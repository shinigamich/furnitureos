// SPDX-License-Identifier: AGPL-3.0-only
// Isometric / exploded isometric assembly views: projection, explode offsets, edges of
// extruded outlines and visibility (see pictorial.hpp for the method and its limits).
#include "fos/drawings/pictorial.hpp"

#include "fos/geometry/contour.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace fos::drawings {
namespace {

constexpr double kEps = core::kLengthEps;
const double kCos30 = std::sqrt(3.0) / 2.0;

/// A straight piece of a (tessellated) outline in the part frame, counter-clockwise.
struct OutlinePiece {
    Vec2 a, b;
    bool cornerAtStart = false; ///< Tangent discontinuity at `a` (a real edge of the solid).
};

Vec2 arcTangent(double angleDeg, double sweep) noexcept {
    const double t = core::degToRad(angleDeg);
    const Vec2 ccw{-std::sin(t), std::cos(t)};
    return sweep > 0 ? ccw : -ccw;
}

Vec2 unit(Vec2 v) noexcept {
    const double l = length(v);
    return l > 0 ? v * (1.0 / l) : v;
}

/// Tessellates a closed contour (made counter-clockwise) into straight pieces.
std::vector<OutlinePiece> tessellateOutline(const geometry::Contour& input, double tol) {
    const geometry::Contour c = geometry::normalizedCcw(input);
    std::vector<OutlinePiece> out;
    const std::size_t n = c.size();
    std::vector<geometry::ContourSegmentInfo> infos;
    infos.reserve(n);
    for (std::size_t i = 0; i < n; ++i) infos.push_back(geometry::segmentInfo(c, i));
    const auto startTangent = [](const geometry::ContourSegmentInfo& s) {
        return s.isArc ? arcTangent(s.startAngle, s.sweep) : unit(s.end - s.start);
    };
    const auto endTangent = [](const geometry::ContourSegmentInfo& s) {
        return s.isArc ? arcTangent(s.startAngle + s.sweep, s.sweep) : unit(s.end - s.start);
    };
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = infos[i];
        const auto& prev = infos[(i + n - 1) % n];
        const Vec2 t0 = endTangent(prev), t1 = startTangent(s);
        const bool corner = std::fabs(t0.x * t1.y - t0.y * t1.x) > 1e-9 || dot(t0, t1) < 0;
        if (!s.isArc) {
            out.push_back({s.start, s.end, corner});
            continue;
        }
        // Chord sagitta r (1 - cos(phi / 2)) <= tol.
        const double sweepRad = std::fabs(core::degToRad(s.sweep));
        const double ratio = std::clamp(1.0 - tol / s.radius, -1.0, 1.0);
        const double maxStep = ratio <= -1.0 ? std::numbers::pi : 2.0 * std::acos(ratio);
        const auto pieces = static_cast<int>(std::clamp(std::ceil(sweepRad / std::max(maxStep, 1e-6)), 1.0, 720.0));
        Vec2 prevPoint = s.start;
        for (int k = 1; k <= pieces; ++k) {
            const double ang = core::degToRad(s.startAngle + s.sweep * k / pieces);
            const Vec2 p = k == pieces ? s.end : s.center + Vec2{std::cos(ang), std::sin(ang)} * s.radius;
            out.push_back({prevPoint, p, k == 1 && corner});
            prevPoint = p;
        }
    }
    return out;
}

/// Andrew's monotone chain; counter-clockwise, collinear points removed.
std::vector<Vec2> hull2d(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return length(a - b) <= kEps; }), pts.end());
    if (pts.size() < 3) return pts;
    const auto cross3 = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<Vec2> h(2 * pts.size());
    std::size_t k = 0;
    for (const Vec2 p : pts) {
        while (k >= 2 && cross3(h[k - 2], h[k - 1], p) <= 1e-9) --k;
        h[k++] = p;
    }
    for (std::size_t i = pts.size() - 1, lower = k + 1; i-- > 0;) {
        while (k >= lower && cross3(h[k - 2], h[k - 1], pts[i]) <= 1e-9) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h;
}

/// Parameter interval of a segment lying STRICTLY inside (by more than kEps) a convex CCW polygon.
std::optional<std::pair<double, double>> insideInterval(Vec2 a, Vec2 b, const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return std::nullopt;
    const Vec2 d = b - a;
    double t0 = 0.0, t1 = 1.0;
    for (std::size_t k = 0; k < poly.size(); ++k) {
        const Vec2 e = poly[(k + 1) % poly.size()] - poly[k];
        const Vec2 nOut = unit(Vec2{e.y, -e.x});
        const double f0 = dot(nOut, a - poly[k]) + kEps; // inside: f0 + t * fd < 0
        const double fd = dot(nOut, d);
        if (std::fabs(fd) < 1e-15) {
            if (f0 >= 0) return std::nullopt;
            continue;
        }
        const double t = -f0 / fd;
        if (fd > 0)
            t1 = std::min(t1, t);
        else
            t0 = std::max(t0, t);
        if (t0 >= t1) return std::nullopt;
    }
    if ((t1 - t0) * length(d) <= kEps) return std::nullopt;
    return std::make_pair(t0, t1);
}

struct Projected2 {
    Vec2 a, b;
};

/// Everything the visibility pass needs about one drawn part.
struct Work {
    const domain::Part* part = nullptr;
    std::size_t index = 0; ///< In Assembly::parts.
    Vec3 offset{};
    std::array<Vec3, 8> corners{}; ///< Bounding box corners, assembly frame (offset applied).
    std::array<Vec3, 3> axes{};
    std::vector<Vec2> hull;
    Rect2 hullBox;
    std::vector<Projected2> edges;
};

/// +1: a is in front of b, -1: b in front of a, 0: they cannot occlude each other.
/// `overlap` is set when the boxes interpenetrate (no separating plane).
int frontOrder(const Work& a, const Work& b, Vec3 toViewer, bool& overlap) {
    std::vector<Vec3> axes(a.axes.begin(), a.axes.end());
    axes.insert(axes.end(), b.axes.begin(), b.axes.end());
    for (const Vec3 u : a.axes)
        for (const Vec3 v : b.axes) axes.push_back(cross(u, v));
    double bestGap = -std::numeric_limits<double>::infinity();
    int bestSign = 0;
    double bestPen = std::numeric_limits<double>::infinity();
    int penSign = 0;
    for (Vec3 n : axes) {
        const double len = std::sqrt(dot(n, n));
        if (len < 1e-9) continue;
        n = n * (1.0 / len);
        double minA = 1e300, maxA = -1e300, minB = 1e300, maxB = -1e300;
        for (const Vec3 p : a.corners) {
            minA = std::min(minA, dot(n, p));
            maxA = std::max(maxA, dot(n, p));
        }
        for (const Vec3 p : b.corners) {
            minB = std::min(minB, dot(n, p));
            maxB = std::max(maxB, dot(n, p));
        }
        const double vn = dot(n, toViewer);
        const bool bAbove = minB - maxA >= minA - maxB; // b on the +n side
        const double gap = std::max(minB - maxA, minA - maxB);
        // Which part lies towards the viewer along +n (0 if n is perpendicular to the view).
        const int sign = std::fabs(vn) < 1e-9 ? 0 : ((vn > 0) == bAbove ? -1 : 1);
        if (gap >= -kEps) {
            if (gap > bestGap + 1e-12) {
                bestGap = gap;
                bestSign = sign;
            }
        } else if (sign != 0) {
            const double pen = -gap;
            if (pen < bestPen - 1e-12) {
                bestPen = pen;
                const double ca = (minA + maxA) * 0.5, cb = (minB + maxB) * 0.5;
                penSign = (cb > ca) == (vn > 0) ? -1 : 1;
            }
        }
    }
    if (bestGap > -std::numeric_limits<double>::infinity()) return bestSign;
    overlap = true;
    return penSign;
}

std::string partLabel(const domain::Part& p) { return "'" + (p.name.empty() ? p.id.str() : p.name) + "'"; }

} // namespace

const char* toString(PictorialKind k) noexcept { return k == PictorialKind::Exploded ? "exploded" : "iso"; }

Vec2 isoProject(IsoDirection d, Vec3 p) noexcept {
    if (d == IsoDirection::FrontRight) return {(p.x + p.y) * kCos30, (p.y - p.x) * 0.5 + p.z};
    return {(p.x - p.y) * kCos30, (p.x + p.y) * 0.5 + p.z};
}

Vec3 isoToViewer(IsoDirection d) noexcept {
    const double s = 1.0 / std::sqrt(3.0);
    return d == IsoDirection::FrontRight ? Vec3{s, -s, s} : Vec3{-s, -s, s};
}

Vec3 explodeDirection(const domain::Part& part, const geometry::Aabb& assemblyBounds) noexcept {
    using R = domain::PartRole;
    switch (part.role) {
    case R::Side: {
        const double dx = part.bounds().center().x - assemblyBounds.center().x;
        if (dx < -kEps) return {-1, 0, 0};
        if (dx > kEps) return {1, 0, 0};
        return {};
    }
    case R::Top:
    case R::Worktop: return {0, 0, 1};
    case R::Bottom:
    case R::Plinth: return {0, 0, -1};
    case R::Back: return {0, 1, 0};
    case R::Door:
    case R::DrawerFront:
    case R::DrawerSide:
    case R::DrawerBack:
    case R::DrawerBottom: return {0, -1, 0};
    case R::Shelf:
    case R::Partition:
    case R::Rail:
    case R::Generic: return {};
    }
    return {};
}

int firstPartListNumber(const domain::Model& model, const domain::AssemblyId& assemblyId) noexcept {
    int number = 1;
    for (const domain::Assembly& a : model.assemblies) {
        if (a.id == assemblyId) return number;
        number += static_cast<int>(a.parts.size());
    }
    return 0;
}

core::Result<PictorialView> generatePictorialView(const domain::Assembly& assembly,
                                                  const PictorialViewOptions& options) {
    const bool exploded = options.kind == PictorialKind::Exploded;
    if (exploded && !(std::isfinite(options.explodeFactor) && options.explodeFactor >= 1.0 &&
                      options.explodeFactor <= 10.0))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Explode factor " + core::formatLength(options.explodeFactor) +
                                   " is not valid; use a value from 1 (not exploded) to 10.",
                               assembly.id.str());
    if (!(std::isfinite(options.chordTolerance) && options.chordTolerance > 0))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Arc chord tolerance " + core::formatLength(options.chordTolerance) +
                                   " mm is not valid; it must be a positive number of millimetres.",
                               assembly.id.str());
    PictorialView view;
    view.kind = options.kind;
    view.direction = options.direction;
    view.explodeFactor = exploded ? options.explodeFactor : 1.0;
    view.id = options.id.empty() ? assembly.id.str() + "/view." + toString(options.kind) : options.id;
    const Vec3 toViewer = isoToViewer(options.direction);
    const geometry::Aabb asmBounds = assembly.bounds();
    const Vec3 asmSize = asmBounds.size();
    const auto project = [&](Vec3 p) { return isoProject(options.direction, p); };

    std::vector<Work> works;
    for (std::size_t i = 0; i < assembly.parts.size(); ++i) {
        const domain::Part& part = assembly.parts[i];
        if (!(part.length > 0 && part.width > 0 && part.thickness > 0) || !std::isfinite(part.length) ||
            !std::isfinite(part.width) || !std::isfinite(part.thickness)) {
            view.diagnostics.push_back({core::Severity::Warning, "drawings.pictorial.empty_part",
                                        "Part " + partLabel(part) + " has a size of " +
                                            core::formatLength(part.length) + " x " + core::formatLength(part.width) +
                                            " x " + core::formatLength(part.thickness) +
                                            " mm, which is not positive, so it is not drawn.",
                                        part.id.str()});
            continue;
        }
        Work w;
        w.part = &part;
        w.index = i;
        if (exploded) {
            const Vec3 dir = explodeDirection(part, asmBounds);
            const double extent = dir.x != 0 ? asmSize.x : dir.y != 0 ? asmSize.y : asmSize.z;
            w.offset = dir * ((options.explodeFactor - 1.0) * extent);
        }
        const geometry::Transform& T = part.placement;
        for (int c = 0; c < 8; ++c) {
            const Vec3 local{(c & 1) ? part.length : 0.0, (c & 2) ? part.width : 0.0, (c & 4) ? part.thickness : 0.0};
            w.corners[static_cast<std::size_t>(c)] = T.applyPoint(local) + w.offset;
        }
        w.axes = {T.xAxis, T.yAxis, T.zAxis};
        std::vector<Vec2> projected;
        for (const Vec3 p : w.corners) projected.push_back(project(p));
        w.hull = hull2d(projected);
        for (const Vec2 p : w.hull) w.hullBox.expand(p);
        view.bounds.expand(w.hullBox);

        // Edges of the extruded outline (part frame -> assembly frame -> view).
        const Vec3 wl{dot(T.xAxis, toViewer), dot(T.yAxis, toViewer), dot(T.zAxis, toViewer)};
        const double zNear = wl.z >= 0 ? part.thickness : 0.0;
        const double zFar = part.thickness - zNear;
        const auto toView = [&](Vec2 q, double z) { return project(T.applyPoint({q.x, q.y, z}) + w.offset); };
        const auto addEdge = [&](Vec2 q0, double z0, Vec2 q1, double z1) {
            const Vec2 a = toView(q0, z0), b = toView(q1, z1);
            if (length(b - a) > kEps) w.edges.push_back({a, b});
        };
        const std::vector<OutlinePiece> outline = tessellateOutline(domain::finishedContour(part), options.chordTolerance);
        std::vector<bool> wallFront(outline.size());
        for (std::size_t k = 0; k < outline.size(); ++k) {
            const Vec2 t = outline[k].b - outline[k].a;
            wallFront[k] = t.y * wl.x - t.x * wl.y > 1e-12 * length(t); // outward normal (t.y, -t.x)
        }
        for (const OutlinePiece& p : outline) addEdge(p.a, zNear, p.b, zNear);
        for (std::size_t k = 0; k < outline.size(); ++k)
            if (wallFront[k]) addEdge(outline[k].a, zFar, outline[k].b, zFar);
        for (std::size_t k = 0; k < outline.size(); ++k) {
            const bool prev = wallFront[(k + outline.size() - 1) % outline.size()], cur = wallFront[k];
            if (outline[k].cornerAtStart ? (prev || cur) : (prev != cur))
                addEdge(outline[k].a, zNear, outline[k].a, zFar);
        }
        if (part.shape)
            for (const geometry::Contour& cut : part.shape->cutouts)
                for (const OutlinePiece& p : tessellateOutline(cut, options.chordTolerance))
                    addEdge(p.a, zNear, p.b, zNear);
        works.push_back(std::move(w));
    }
    if (works.empty())
        return core::makeError(core::ErrorCode::NotFound,
                               "Assembly '" + (assembly.name.empty() ? assembly.id.str() : assembly.name) +
                                   "' has no part with a positive size, so there is nothing to draw.",
                               assembly.id.str());

    // Painter's order per pair: occluders[i] = parts in front of part i.
    std::vector<std::vector<std::size_t>> occluders(works.size());
    std::size_t overlaps = 0;
    std::string firstOverlap;
    for (std::size_t i = 0; i < works.size(); ++i)
        for (std::size_t j = i + 1; j < works.size(); ++j) {
            if (!intersects(works[i].hullBox, works[j].hullBox)) continue;
            bool overlap = false;
            const int order = frontOrder(works[i], works[j], toViewer, overlap);
            if (overlap && overlaps++ == 0)
                firstOverlap = partLabel(*works[i].part) + " and " + partLabel(*works[j].part);
            if (order > 0) occluders[j].push_back(i);
            if (order < 0) occluders[i].push_back(j);
        }
    if (overlaps > 0)
        view.diagnostics.push_back(
            {core::Severity::Info, "drawings.pictorial.overlap",
             std::to_string(overlaps) + " pair(s) of parts interpenetrate (first: " + firstOverlap +
                 "); their visibility in the " + toString(options.kind) +
                 " view is approximated by ordering them along the axis of least penetration.",
             view.id});

    for (std::size_t i = 0; i < works.size(); ++i) {
        const Work& w = works[i];
        PictorialPart pp;
        pp.partId = w.part->id;
        pp.name = w.part->name;
        pp.role = w.part->role;
        pp.number = options.firstPartNumber + static_cast<int>(w.index);
        pp.offset = w.offset;
        pp.silhouette = w.hull;
        double longest = 0;
        for (const Projected2& e : w.edges) {
            std::vector<std::pair<double, double>> hidden;
            for (const std::size_t o : occluders[i])
                if (const auto iv = insideInterval(e.a, e.b, works[o].hull)) hidden.push_back(*iv);
            std::sort(hidden.begin(), hidden.end());
            const Vec2 d = e.b - e.a;
            double cursor = 0;
            const auto emit = [&](double t0, double t1) {
                if ((t1 - t0) * length(d) <= kEps) return;
                ViewSegment s{e.a + d * t0, e.a + d * t1, LineStyle::Visible, w.part->id.str()};
                const double len = length(s.b - s.a);
                if (len > longest + 1e-9) {
                    longest = len;
                    pp.balloonAnchor = (s.a + s.b) * 0.5;
                }
                view.segments.push_back(std::move(s));
            };
            for (const auto& h : hidden) {
                if (h.first > cursor) emit(cursor, h.first);
                cursor = std::max(cursor, h.second);
            }
            emit(cursor, 1.0);
        }
        view.parts.push_back(std::move(pp));
    }
    return view;
}

} // namespace fos::drawings
