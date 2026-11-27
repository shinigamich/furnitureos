// SPDX-License-Identifier: AGPL-3.0-only
#include "placer.hpp"

#include "fos/geometry/offset.hpp"
#include "fos/geometry/segment.hpp"
#include "fos/nesting/nest.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fos::nesting::detail {

namespace {

constexpr double kEps = core::kLengthEps;

/// Euclidean distance between two (non-empty) rectangles; 0 when they touch or overlap.
double rectDistance(const Rect2& a, const Rect2& b) noexcept {
    const double dx = std::max({0.0, a.min.x - b.max.x, b.min.x - a.max.x});
    const double dy = std::max({0.0, a.min.y - b.max.y, b.min.y - a.max.y});
    return std::hypot(dx, dy);
}

bool boxesMeet(const Rect2& a, const Rect2& b, double tol) noexcept {
    return a.min.x <= b.max.x + tol && b.min.x <= a.max.x + tol && a.min.y <= b.max.y + tol &&
           b.min.y <= a.max.y + tol;
}

/// p lies inside the convex CCW polygon by more than `depth` from every edge.
bool deepInsideConvex(const Polygon2& c, Vec2 p, double depth) noexcept {
    const std::size_t n = c.size();
    if (n < 3) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = c[i], b = c[(i + 1) % n];
        const double len = geometry::length(b - a);
        if (len == 0) continue;
        if (geometry::cross(b - a, p - a) <= depth * len) return false;
    }
    return true;
}

/// Proper intersection point of two non-parallel segments (no collinear handling: collinear
/// overlaps contribute their endpoints as vertex candidates anyway).
bool segIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2& out) noexcept {
    const Vec2 r = b - a, s = d - c;
    const double den = geometry::cross(r, s);
    if (std::fabs(den) <= 1e-12 * geometry::length(r) * geometry::length(s)) return false;
    const double t = geometry::cross(c - a, s) / den;
    const double u = geometry::cross(c - a, r) / den;
    if (t < -1e-12 || t > 1 + 1e-12 || u < -1e-12 || u > 1 + 1e-12) return false;
    out = a + r * t;
    return true;
}

/// Intersections of segment a-b with the vertical line x = X (within the y-range [y0, y1]).
void crossVertical(Vec2 a, Vec2 b, double X, double y0, double y1, std::vector<Vec2>& out) {
    if ((a.x - X) * (b.x - X) > 0 || a.x == b.x) return;
    const double t = (X - a.x) / (b.x - a.x);
    const double y = a.y + (b.y - a.y) * t;
    if (y >= y0 - kEps && y <= y1 + kEps) out.push_back({X, y});
}
void crossHorizontal(Vec2 a, Vec2 b, double Y, double x0, double x1, std::vector<Vec2>& out) {
    if ((a.y - Y) * (b.y - Y) > 0 || a.y == b.y) return;
    const double t = (Y - a.y) / (b.y - a.y);
    const double x = a.x + (b.x - a.x) * t;
    if (x >= x0 - kEps && x <= x1 + kEps) out.push_back({x, Y});
}

/// Minimum distance between the BOUNDARIES of two polygons (also when one contains the other).
double boundaryDistance(const Polygon2& a, const Polygon2& b) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < a.size(); ++i) {
        const geometry::Segment2 sa{a[i], a[(i + 1) % a.size()]};
        for (std::size_t j = 0; j < b.size(); ++j) {
            best = std::min(best, geometry::closestPointsSegments(sa, {b[j], b[(j + 1) % b.size()]}).distance);
            if (best == 0) return 0;
        }
    }
    return best;
}

/// `inner` lies inside one of the holes, at least s from that hole's boundary (s = 0: inside or on it).
bool insideSomeHole(const Polygon2& inner, const Rect2& innerBox, const std::vector<Polygon2>& holes,
                    const std::vector<Rect2>& boxes, double s) {
    for (std::size_t i = 0; i < holes.size(); ++i) {
        const Rect2& hb = boxes[i];
        if (innerBox.min.x < hb.min.x + s - kEps || innerBox.min.y < hb.min.y + s - kEps ||
            innerBox.max.x > hb.max.x - s + kEps || innerBox.max.y > hb.max.y - s + kEps)
            continue;
        if (!geometry::polygonContainsPolygon(holes[i], inner)) continue;
        if (s <= kEps || boundaryDistance(inner, holes[i]) >= s - kEps) return true;
    }
    return false;
}

using Key = std::array<double, 3>;

/// a < b lexicographically with tolerance kEps per component.
bool keyLess(const Key& a, const Key& b) noexcept {
    for (std::size_t i = 0; i < 3; ++i) {
        if (a[i] < b[i] - kEps) return true;
        if (a[i] > b[i] + kEps) return false;
    }
    return false;
}

} // namespace

bool Score::betterThan(const Score& o) const noexcept {
    if (unplaced != o.unplaced) return unplaced < o.unplaced;
    if (sheets != o.sheets) return sheets < o.sheets;
    if (std::fabs(sheetArea - o.sheetArea) > 1e-6) return sheetArea < o.sheetArea;
    return lastLength < o.lastLength - kEps;
}

Score scoreOf(const Job& job, const Decoded& d) {
    Score s;
    s.unplaced = static_cast<int>(d.unplaced.size());
    s.sheets = static_cast<int>(d.sheets.size());
    for (const auto& sh : d.sheets) {
        const Sheet& st = job.sheets[static_cast<std::size_t>(sh.stock)];
        s.sheetArea += st.length * st.width;
    }
    if (!d.sheets.empty())
        for (const auto& it : d.sheets.back().items) s.lastLength = std::max(s.lastLength, it.box.max.x);
    return s;
}

Placer::Placer(const Job& job, const std::vector<Shape>& shapes, const NestOptions& options)
    : job_(job), shapes_(shapes), options_(options) {
    for (const Sheet& s : job.sheets) areas_.push_back(placementArea(job, s));
    fitsEmpty_.assign(shapes.size(), std::vector<bool>(job.sheets.size(), false));
    for (std::size_t p = 0; p < shapes.size(); ++p)
        for (std::size_t t = 0; t < areas_.size(); ++t)
            for (const auto& o : shapes[p].orients)
                if (!areas_[t].empty() && o.w <= areas_[t].width() + kEps && o.h <= areas_[t].height() + kEps)
                    fitsEmpty_[p][t] = true;
}

const std::vector<Placer::Nfp>& Placer::nfps(int partQ, int orientQ, int partP, int orientP) {
    const std::array<int, 4> key{partQ, orientQ, partP, orientP};
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    std::vector<Nfp> list;
    const auto& oq = shapes_[static_cast<std::size_t>(partQ)].orients[static_cast<std::size_t>(orientQ)];
    const auto& op = shapes_[static_cast<std::size_t>(partP)].orients[static_cast<std::size_t>(orientP)];
    for (const auto& qp : oq.pieces)
        for (const auto& pp : op.pieces) {
            Polygon2 neg = pp;
            for (auto& v : neg.vertices) v = -v;
            Nfp n;
            n.raw = minkowskiSumConvex(qp, neg);
            n.grown = n.raw;
            if (job_.partSpacing > kEps && n.raw.size() >= 3) {
                auto g = geometry::offsetPolygon(n.raw, job_.partSpacing);
                if (g) n.grown = std::move(g).value();
            }
            n.box = geometry::bounds(n.grown);
            list.push_back(std::move(n));
        }
    return cache_.emplace(key, std::move(list)).first->second;
}

void Placer::fillHoles(PlacedItem& item, double rot, bool mirrored, Vec2 t) const {
    item.holes.clear();
    item.holeBoxes.clear();
    if (!options_.partInPart) return;
    for (const auto& h : job_.parts[static_cast<std::size_t>(item.part)].holes) {
        item.holes.push_back(transformOutline(h, rot, mirrored, t));
        item.holeBoxes.push_back(geometry::bounds(item.holes.back()));
    }
}

bool Placer::positionValid(const Polygon2& outline, const Rect2& box, int part, double rot, bool mirrored, Vec2 t,
                           const OpenSheet& sheet) const {
    const double s = job_.partSpacing;
    PlacedItem self; // this part's own holes, built lazily (another part may sit inside one of them)
    bool selfHoles = false;
    for (const auto& q : sheet.items) {
        if (rectDistance(box, q.box) >= s + 1e-9) continue;
        const bool ok = s > kEps ? geometry::polygonDistance(outline, q.outline) >= s - kEps
                                 : !geometry::polygonsOverlap(outline, q.outline);
        if (ok) continue;
        if (!options_.partInPart) return false;
        if (insideSomeHole(outline, box, q.holes, q.holeBoxes, s)) continue;
        if (!selfHoles) {
            self.part = part;
            fillHoles(self, rot, mirrored, t);
            selfHoles = true;
        }
        if (insideSomeHole(q.outline, q.box, self.holes, self.holeBoxes, s)) continue;
        return false;
    }
    return true;
}

bool Placer::tryPlace(OpenSheet& sheet, int part, PlacedItem& out, PlacementScore policy) {
    const Shape& shape = shapes_[static_cast<std::size_t>(part)];
    const Rect2 area = areas_[static_cast<std::size_t>(sheet.stock)];
    const Part& src = job_.parts[static_cast<std::size_t>(part)];
    const double s = job_.partSpacing;
    bool found = false;
    Key best{};

    // Used bounding box of the sheet (for MinBoundingBox), anchored at the placement-area corner.
    double usedX = area.min.x, usedY = area.min.y;
    for (const auto& q : sheet.items) {
        usedX = std::max(usedX, q.box.max.x);
        usedY = std::max(usedY, q.box.max.y);
    }
    const auto keyOf = [&](Vec2 c, double w, double h) -> Key {
        switch (policy) {
        case PlacementScore::BottomLeft: return {c.y + h, c.x, 0.0};
        case PlacementScore::MinBoundingBox:
            return {(std::max(usedX, c.x + w) - area.min.x) * (std::max(usedY, c.y + h) - area.min.y), c.x + w, c.y};
        case PlacementScore::MinRightEdge: break;
        }
        return {c.x + w, c.y, 0.0};
    };

    struct Rel {
        Polygon2 raw, grown;
        Rect2 box;
        bool holey; ///< From a part with cut-outs (partInPart): no cheap deep-inside reject.
    };
    std::vector<Rel> rel;
    std::vector<Vec2> cand;
    for (std::size_t oi = 0; oi < shape.orients.size(); ++oi) {
        if (cancelled()) return false;
        const Orientation& o = shape.orients[oi];
        if (o.w > area.width() + kEps || o.h > area.height() + kEps) continue;
        // Keys grow with x and y, so the inner-fit corner bounds every candidate of this orientation.
        if (found && !keyLess(keyOf(area.min, o.w, o.h), best)) continue;
        const Rect2 ifp{area.min, {std::max(area.min.x, area.max.x - o.w), std::max(area.min.y, area.max.y - o.h)}};

        rel.clear();
        for (const auto& q : sheet.items)
            for (const auto& n : nfps(q.part, q.orient, part, static_cast<int>(oi))) {
                Rect2 b{n.box.min + q.pos, n.box.max + q.pos};
                if (!boxesMeet(b, ifp, kEps)) continue;
                rel.push_back({geometry::translated(n.raw, q.pos), geometry::translated(n.grown, q.pos), b,
                               !q.holes.empty()});
            }

        cand.clear();
        cand.push_back(ifp.min);
        cand.push_back({ifp.max.x, ifp.min.y});
        cand.push_back({ifp.min.x, ifp.max.y});
        cand.push_back(ifp.max);
        for (std::size_t i = 0; i < rel.size(); ++i) {
            const Polygon2& g = rel[i].grown;
            for (std::size_t e = 0; e < g.size(); ++e) {
                const Vec2 a = g[e], b = g[(e + 1) % g.size()];
                cand.push_back(a);
                crossVertical(a, b, ifp.min.x, ifp.min.y, ifp.max.y, cand);
                crossVertical(a, b, ifp.max.x, ifp.min.y, ifp.max.y, cand);
                crossHorizontal(a, b, ifp.min.y, ifp.min.x, ifp.max.x, cand);
                crossHorizontal(a, b, ifp.max.y, ifp.min.x, ifp.max.x, cand);
            }
            for (std::size_t j = i + 1; j < rel.size(); ++j) {
                if (!boxesMeet(rel[i].box, rel[j].box, kEps)) continue;
                const Polygon2& h = rel[j].grown;
                for (std::size_t e = 0; e < g.size(); ++e) {
                    const Vec2 a = g[e], b = g[(e + 1) % g.size()];
                    const Rect2 eb = Rect2::fromCorners(a, b);
                    if (!boxesMeet(eb, rel[j].box, kEps) || !boxesMeet(eb, ifp, kEps)) continue;
                    for (std::size_t f = 0; f < h.size(); ++f) {
                        const Vec2 c = h[f], d = h[(f + 1) % h.size()];
                        if (!boxesMeet(eb, Rect2::fromCorners(c, d), kEps)) continue;
                        Vec2 x;
                        if (segIntersect(a, b, c, d, x)) cand.push_back(x);
                    }
                }
            }
        }
        // Part-in-part: SAMPLED candidates inside every cut-out of the placed parts (not an exact
        // inner-fit polygon): corners of the hole bbox inset by the spacing, the hole vertices offset
        // by the spacing toward each bbox corner of the part, and a 9 x 9 grid over the inset box.
        if (options_.partInPart)
            for (const auto& q : sheet.items)
                for (std::size_t hi = 0; hi < q.holes.size(); ++hi) {
                    const Rect2& hb = q.holeBoxes[hi];
                    const Rect2 r{{hb.min.x + s, hb.min.y + s}, {hb.max.x - s - o.w, hb.max.y - s - o.h}};
                    if (r.min.x > r.max.x + kEps || r.min.y > r.max.y + kEps) continue;
                    for (int gx = 0; gx <= 8; ++gx)
                        for (int gy = 0; gy <= 8; ++gy)
                            cand.push_back({r.min.x + (r.max.x - r.min.x) * gx / 8.0, r.min.y + (r.max.y - r.min.y) * gy / 8.0});
                    for (Vec2 v : q.holes[hi].vertices) {
                        cand.push_back({v.x + s, v.y + s});
                        cand.push_back({v.x - s - o.w, v.y + s});
                        cand.push_back({v.x + s, v.y - s - o.h});
                        cand.push_back({v.x - s - o.w, v.y - s - o.h});
                    }
                }
        // Candidates inside the inner-fit rectangle (clamped) that could beat the best key so far,
        // tried in key order: the first valid one is this orientation's best.
        struct Cand {
            Key key;
            Vec2 c;
        };
        std::vector<Cand> pts;
        pts.reserve(cand.size());
        for (Vec2 c : cand) {
            if (!ifp.contains(c, kEps)) continue;
            const Vec2 cc{std::clamp(c.x, ifp.min.x, ifp.max.x), std::clamp(c.y, ifp.min.y, ifp.max.y)};
            const Key k = keyOf(cc, o.w, o.h);
            if (found && !keyLess(k, best)) continue;
            pts.push_back({k, cc});
        }
        std::sort(pts.begin(), pts.end(), [](const Cand& a, const Cand& b) {
            for (std::size_t i = 0; i < 3; ++i)
                if (a.key[i] != b.key[i]) return a.key[i] < b.key[i];
            return a.c.x != b.c.x ? a.c.x < b.c.x : a.c.y < b.c.y;
        });
        Vec2 prev{std::numeric_limits<double>::quiet_NaN(), 0};
        for (const Cand& k : pts) {
            const Vec2 c = k.c;
            if (std::fabs(c.x - prev.x) <= 1e-9 && std::fabs(c.y - prev.y) <= 1e-9) continue;
            prev = c;
            if (found && !keyLess(k.key, best)) break;
            bool deep = false;
            for (const auto& r : rel)
                if (!r.holey && r.box.contains(c) && deepInsideConvex(r.raw, c, 1e-5)) {
                    deep = true;
                    break;
                }
            if (deep) continue;
            const Vec2 t = c - o.offset;
            Polygon2 outline = transformOutline(src.outline, o.rotationDeg, o.mirrored, t);
            const Rect2 box = geometry::bounds(outline);
            if (!positionValid(outline, box, part, o.rotationDeg, o.mirrored, t, sheet)) continue;
            found = true;
            best = k.key;
            out.part = part;
            out.orient = static_cast<int>(oi);
            out.pos = c;
            out.outline = std::move(outline);
            out.box = box;
            fillHoles(out, o.rotationDeg, o.mirrored, t);
            break;
        }
    }
    return found;
}

bool Placer::cancelled() {
    if (!cancelled_ && options_.isCancelled && options_.isCancelled()) cancelled_ = true;
    return cancelled_;
}

Decoded Placer::decode(const std::vector<Instance>& order, PlacementScore policy) {
    Decoded d;
    std::vector<int> remaining;
    for (const Sheet& s : job_.sheets) remaining.push_back(s.quantity);
    for (const Instance& inst : order) {
        if (cancelled()) {
            d.aborted = true;
            return d;
        }
        // Net area with part-in-part (the part's own cut-outs stay free for others), else outline area.
        const auto& shp = shapes_[static_cast<std::size_t>(inst.part)];
        const double a = options_.partInPart ? shp.netArea : shp.area;
        PlacedItem item;
        bool placed = false;
        for (auto& sh : d.sheets) {
            const auto pi = static_cast<std::size_t>(inst.part);
            if (sh.failed[pi] || sh.freeArea < a - 1e-6) continue;
            if (!tryPlace(sh, inst.part, item, policy)) {
                sh.failed[pi] = !cancelled_;
            } else {
                item.inst = inst;
                sh.freeArea -= a;
                sh.items.push_back(std::move(item));
                placed = true;
                break;
            }
        }
        for (std::size_t t = 0; !placed && t < job_.sheets.size(); ++t) {
            if (!fitsEmpty_[static_cast<std::size_t>(inst.part)][t] || remaining[t] == 0) continue;
            OpenSheet sh;
            sh.stock = static_cast<int>(t);
            sh.freeArea = areas_[t].area();
            sh.failed.assign(shapes_.size(), false);
            if (!tryPlace(sh, inst.part, item, policy)) continue;
            if (remaining[t] > 0) --remaining[t];
            item.inst = inst;
            sh.freeArea -= a;
            sh.items.push_back(std::move(item));
            d.sheets.push_back(std::move(sh));
            placed = true;
        }
        if (cancelled()) {
            d.aborted = true;
            return d;
        }
        if (!placed) d.unplaced.push_back(inst);
    }
    return d;
}

} // namespace fos::nesting::detail
