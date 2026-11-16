// SPDX-License-Identifier: AGPL-3.0-only
// ContourSpec -> domain::PartShape (README "Contoured parts").
#include "fos/furniture/contour.hpp"

#include "fos/geometry/curve_intersect.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace fos::furniture {
namespace {

using domain::EdgeSide;
using geometry::Contour;
using geometry::ContourVertex;
using geometry::Vec2;
using Band = std::optional<domain::EdgeBandId>;

std::string num(double v) {
    if (std::isfinite(v) && std::fabs(v) < 1e12) return core::formatLength(v, 3);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

constexpr const char* kCornerNames[4] = {"L1/W1", "L1/W2", "L2/W2", "L2/W1"};

core::Error invalid(const std::string& label, const std::string& message, core::ErrorCode code = core::ErrorCode::InvalidArgument) {
    return core::makeError(code, "furniture.contour.invalid: " + label + ": " + message, label);
}

/// Checks that `text` parses; an empty text is accepted only when `allowEmpty`.
core::Result<void> checkExpression(const std::string& text, const std::string& field, bool allowEmpty,
                                   const std::string& label) {
    if (text.empty()) {
        if (allowEmpty) return {};
        return invalid(label, field + " needs an expression (mm).");
    }
    auto parsed = parametric::parseExpression(text);
    if (!parsed)
        return invalid(label, field + " expression '" + text + "' cannot be parsed: " + parsed.error().message,
                       parsed.error().code);
    return {};
}

std::string cutoutName(std::size_t i, const CutoutSpec& c) {
    return "cut-out " + std::to_string(i + 1) + " (" + toString(c.kind) + ")";
}

/// Evaluates contour expressions over the owner's graph plus the contour_param names.
struct Evaluator {
    const parametric::Lookup& parameters;
    double L, W, T;
    const std::string& label;

    core::Result<double> operator()(const std::string& text, const std::string& field, bool emptyIsZero = false) const {
        if (text.empty() && emptyIsZero) return 0.0;
        auto parsed = parametric::parseExpression(text);
        if (!parsed)
            return invalid(label, field + " expression '" + text + "' cannot be parsed: " + parsed.error().message,
                           parsed.error().code);
        const parametric::Lookup lookup = [this](const std::string& name) -> std::optional<double> {
            if (name == contour_param::kLength) return L;
            if (name == contour_param::kWidth) return W;
            if (name == contour_param::kThickness) return T;
            return parameters ? parameters(name) : std::nullopt;
        };
        auto v = parametric::evaluate(*parsed.value(), lookup);
        if (!v)
            return invalid(label, field + " expression '" + text + "' cannot be evaluated: " + v.error().message,
                           v.error().code);
        if (!std::isfinite(v.value()))
            return invalid(label, field + " expression '" + text + "' is not finite.");
        return v.value();
    }
};

struct Outline {
    Contour contour;
    std::vector<Band> bands; ///< One per segment.
};

/// Drops zero-length segments (vertex i equal to vertex i + 1) together with their bands.
Outline compact(const Outline& raw) {
    Outline out;
    const std::size_t n = raw.contour.size();
    for (std::size_t i = 0; i < n; ++i)
        if (!geometry::approxEqual(raw.contour.vertices[i].point, raw.contour.vertices[(i + 1) % n].point)) {
            out.contour.vertices.push_back(raw.contour.vertices[i]);
            out.bands.push_back(raw.bands[i]);
        }
    return out;
}

core::Result<Outline> roundedOutline(const ContourSpec& spec, const Evaluator& ev, const domain::Part& part) {
    const double L = ev.L, W = ev.W;
    double r[4];
    for (int k = 0; k < 4; ++k) {
        const std::string field = std::string("corner radius ") + kCornerNames[k];
        auto v = ev(spec.radii[static_cast<std::size_t>(k)], field, true);
        if (!v) return v.error();
        if (v.value() < 0)
            return invalid(ev.label, field + " is " + num(v.value()) + " mm (expression '" +
                                         spec.radii[static_cast<std::size_t>(k)] + "'); radii must be >= 0.");
        r[k] = v.value();
    }
    struct SideFit {
        int a, b;
        double length;
        const char* edge;
    };
    const SideFit sides[4] = {{0, 1, L, "L1"}, {1, 2, W, "W2"}, {2, 3, L, "L2"}, {3, 0, W, "W1"}};
    for (const SideFit& s : sides)
        if (r[s.a] + r[s.b] > s.length + core::kLengthEps)
            return invalid(ev.label, std::string("corner radii ") + kCornerNames[s.a] + " = " + num(r[s.a]) + " mm and " +
                                         kCornerNames[s.b] + " = " + num(r[s.b]) + " mm add up to " +
                                         num(r[s.a] + r[s.b]) + " mm, more than the " + num(s.length) + " mm of edge " +
                                         s.edge + ". Reduce the radii.");
    const auto band = [&](EdgeSide e) { return part.edge(e); };
    // Corner arc band: its L side (L1 / L2) when banded, otherwise its W side.
    const EdgeSide cornerL[4] = {EdgeSide::L1, EdgeSide::L1, EdgeSide::L2, EdgeSide::L2};
    const EdgeSide cornerW[4] = {EdgeSide::W1, EdgeSide::W2, EdgeSide::W2, EdgeSide::W1};
    const auto cornerBand = [&](int k) { return band(cornerL[k]) ? band(cornerL[k]) : band(cornerW[k]); };
    // Straight k runs from corner k to corner k + 1: L1, W2, L2, W1.
    const EdgeSide straight[4] = {EdgeSide::L1, EdgeSide::W2, EdgeSide::L2, EdgeSide::W1};
    const Vec2 in[4] = {{0, r[0]}, {L - r[1], 0}, {L, W - r[2]}, {r[3], W}};
    const Vec2 out[4] = {{r[0], 0}, {L, r[1]}, {L - r[2], W}, {0, W - r[3]}};
    const double q = std::tan(core::kPi / 8.0); // 90 degree counter-clockwise arc
    Outline raw;
    for (int k = 0; k < 4; ++k) {
        const int next = (k + 1) % 4;
        raw.contour.vertices.push_back({out[k], 0.0});
        raw.bands.push_back(band(straight[k]));
        if (r[next] > 0) {
            raw.contour.vertices.push_back({in[next], q});
            raw.bands.push_back(cornerBand(next));
        }
    }
    return compact(raw);
}

core::Result<Outline> arcEdgeOutline(const ContourSpec& spec, const Evaluator& ev, const domain::Part& part) {
    const double L = ev.L, W = ev.W;
    const std::string edge = domain::toString(spec.arcEdge);
    auto riseR = ev(spec.arcRise, "arc rise of edge " + edge);
    if (!riseR) return riseR.error();
    const double rise = riseR.value();
    // Segment index of the edge in the counter-clockwise rectangle (0,0),(L,0),(L,W),(0,W).
    std::size_t s = 0;
    switch (spec.arcEdge) {
    case EdgeSide::L1: s = 0; break;
    case EdgeSide::W2: s = 1; break;
    case EdgeSide::L2: s = 2; break;
    case EdgeSide::W1: s = 3; break;
    }
    const bool alongLength = s % 2 == 0;
    const double chord = alongLength ? L : W, across = alongLength ? W : L;
    if (!(rise > core::kLengthEps))
        return invalid(ev.label, "arc rise of edge " + edge + " is " + num(rise) + " mm (expression '" + spec.arcRise +
                                     "'); it must be greater than 0.");
    if (rise > chord / 2 + core::kLengthEps)
        return invalid(ev.label, "arc rise of edge " + edge + " is " + num(rise) + " mm, more than half the " +
                                     num(chord) + " mm edge (" + num(chord / 2) +
                                     " mm, a half circle). Reduce the rise.");
    if (rise >= across - core::kLengthEps)
        return invalid(ev.label, "arc rise of edge " + edge + " is " + num(rise) + " mm but the part is only " +
                                     num(across) + " mm across that edge. Reduce the rise.");
    std::array<Vec2, 4> p{Vec2{0, 0}, Vec2{L, 0}, Vec2{L, W}, Vec2{0, W}};
    const Vec2 inward[4] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
    // Bulge = sagitta / half chord (tan(theta / 4) = s / (c / 2)), capped at a half circle.
    const double b = std::min(1.0, rise / (chord / 2));
    Outline o;
    if (spec.arcDirection == ArcDirection::Convex) {
        // Cut INTO the box: both ends of the edge move inwards by the rise, the apex stays on
        // the edge line, so the bounding box is still [0,L]x[0,W].
        p[s] = p[s] + inward[s] * rise;
        p[(s + 1) % 4] = p[(s + 1) % 4] + inward[s] * rise;
    }
    const EdgeSide sides[4] = {EdgeSide::L1, EdgeSide::W2, EdgeSide::L2, EdgeSide::W1};
    for (std::size_t i = 0; i < 4; ++i) {
        const double bulge = i == s ? (spec.arcDirection == ArcDirection::Convex ? b : -b) : 0.0;
        o.contour.vertices.push_back({p[i], bulge});
        o.bands.push_back(part.edge(sides[i]));
    }
    return o;
}

core::Result<Outline> customOutline(const ContourSpec& spec, const Evaluator& ev, const domain::Part& part) {
    const double L = ev.L, W = ev.W;
    Outline o;
    for (std::size_t i = 0; i < spec.vertices.size(); ++i) {
        const auto& vs = spec.vertices[i];
        const std::string field = "vertex " + std::to_string(i + 1);
        auto x = ev(vs.x, field + " x");
        if (!x) return x.error();
        auto y = ev(vs.y, field + " y");
        if (!y) return y.error();
        o.contour.vertices.push_back({{x.value(), y.value()}, vs.bulge});
    }
    if (auto v = geometry::validateContour(o.contour); !v)
        return invalid(ev.label, "the custom outline is not a valid contour: " + v.error().message);
    if (!(geometry::signedArea(o.contour) > 0))
        return invalid(ev.label, "the custom outline runs clockwise; list its vertices counter-clockwise.");
    const geometry::Rect2 b = geometry::bounds(o.contour);
    constexpr double tol = 1e-6;
    if (std::fabs(b.min.x) > tol || std::fabs(b.min.y) > tol || std::fabs(b.max.x - L) > tol || std::fabs(b.max.y - W) > tol)
        return invalid(ev.label, "the custom outline spans (" + num(b.min.x) + ", " + num(b.min.y) + ") .. (" +
                                     num(b.max.x) + ", " + num(b.max.y) + ") but must span exactly (0, 0) .. (" + num(L) +
                                     ", " + num(W) + "), the part's finished size. Use contour.length / contour.width.");
    const std::size_t n = o.contour.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& vs = spec.vertices[i];
        Band band;
        if (vs.bandFrom) band = part.edge(*vs.bandFrom);
        else if (vs.bulge == 0.0) {
            const Vec2 a = o.contour.vertices[i].point, c = o.contour.vertices[(i + 1) % n].point;
            const auto on = [&](double u, double w, double target) {
                return std::fabs(u - target) <= tol && std::fabs(w - target) <= tol;
            };
            if (on(a.y, c.y, 0)) band = part.edge(EdgeSide::L1);
            else if (on(a.y, c.y, W)) band = part.edge(EdgeSide::L2);
            else if (on(a.x, c.x, 0)) band = part.edge(EdgeSide::W1);
            else if (on(a.x, c.x, L)) band = part.edge(EdgeSide::W2);
        }
        o.bands.push_back(band);
    }
    return o;
}

core::Result<Contour> buildCutout(const CutoutSpec& c, const std::string& name, const Evaluator& ev) {
    auto x = ev(c.x, name + " x");
    if (!x) return x.error();
    auto y = ev(c.y, name + " y");
    if (!y) return y.error();
    if (c.kind == CutoutKind::Circle) {
        auto r = ev(c.radius, name + " radius");
        if (!r) return r.error();
        if (!(r.value() > core::kLengthEps))
            return invalid(ev.label, name + " radius is " + num(r.value()) + " mm; it must be greater than 0.");
        return geometry::circleContour({x.value(), y.value()}, r.value());
    }
    auto l = ev(c.length, name + " length");
    if (!l) return l.error();
    auto w = ev(c.width, name + " width");
    if (!w) return w.error();
    auto r = ev(c.radius, name + " corner radius", true);
    if (!r) return r.error();
    if (!(l.value() > core::kLengthEps) || !(w.value() > core::kLengthEps))
        return invalid(ev.label, name + " is " + num(l.value()) + " x " + num(w.value()) +
                                     " mm; both sizes must be greater than 0.");
    if (r.value() < 0 || 2 * r.value() > std::min(l.value(), w.value()) + core::kLengthEps)
        return invalid(ev.label, name + " corner radius is " + num(r.value()) + " mm; it must be between 0 and half the "
                                     "smaller side (" + num(std::min(l.value(), w.value()) / 2) + " mm).");
    const double rr[4] = {r.value(), r.value(), r.value(), r.value()};
    auto rect = geometry::roundedRectangleContour(l.value(), w.value(), rr);
    if (!rect) return invalid(ev.label, name + ": " + rect.error().message);
    return geometry::translated(rect.value(), {x.value(), y.value()});
}

bool segmentsTouch(const Contour& a, const Contour& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto sa = geometry::segmentInfo(a, i);
        for (std::size_t j = 0; j < b.size(); ++j)
            if (geometry::intersectContourSegments(sa, geometry::segmentInfo(b, j)).kind !=
                geometry::CurveIntersectionKind::None)
                return true;
    }
    return false;
}

std::string describeCutout(const Contour& c) {
    const geometry::Rect2 b = geometry::bounds(c);
    return "(" + num(b.min.x) + ", " + num(b.min.y) + ") .. (" + num(b.max.x) + ", " + num(b.max.y) + ")";
}

} // namespace

std::size_t contourSegmentEstimate(const ContourSpec& spec) noexcept {
    const std::size_t outer = spec.kind == ContourKind::Custom ? spec.vertices.size() : 8;
    return outer + 8 * spec.cutouts.size();
}

core::Result<void> checkContourSpec(const ContourSpec& spec, const std::string& label) {
    switch (spec.kind) {
    case ContourKind::Rectangle: break;
    case ContourKind::RoundedCorners:
        for (std::size_t k = 0; k < 4; ++k)
            if (auto r = checkExpression(spec.radii[k], std::string("corner radius ") + kCornerNames[k], true, label); !r)
                return r;
        break;
    case ContourKind::ArcEdge:
        if (auto r = checkExpression(spec.arcRise, "arc rise of edge " + std::string(domain::toString(spec.arcEdge)),
                                     false, label);
            !r)
            return r;
        break;
    case ContourKind::Custom: {
        const std::size_t n = spec.vertices.size();
        if (n > limits::kMaxContourVertices)
            return invalid(label, "the custom outline has " + std::to_string(n) + " vertices; the limit is " +
                                      std::to_string(limits::kMaxContourVertices) + ".");
        if (n < 3)
            return invalid(label, "a custom outline needs at least 3 vertices (has " + std::to_string(n) + ").");
        for (std::size_t i = 0; i < n; ++i) {
            const auto& v = spec.vertices[i];
            const std::string field = "vertex " + std::to_string(i + 1);
            if (auto r = checkExpression(v.x, field + " x", false, label); !r) return r;
            if (auto r = checkExpression(v.y, field + " y", false, label); !r) return r;
            if (!std::isfinite(v.bulge) || std::fabs(v.bulge) > 1.0)
                return invalid(label, field + " has bulge " + num(v.bulge) +
                                          "; a bulge must be finite with |bulge| <= 1 (at most a half circle).");
        }
        break;
    }
    }
    if (spec.cutouts.size() > limits::kMaxContourCutouts)
        return invalid(label, "the contour has " + std::to_string(spec.cutouts.size()) + " cut-outs; the limit is " +
                                  std::to_string(limits::kMaxContourCutouts) + ".");
    for (std::size_t i = 0; i < spec.cutouts.size(); ++i) {
        const CutoutSpec& c = spec.cutouts[i];
        const std::string name = cutoutName(i, c);
        if (auto r = checkExpression(c.x, name + " x", false, label); !r) return r;
        if (auto r = checkExpression(c.y, name + " y", false, label); !r) return r;
        if (c.kind == CutoutKind::Circle) {
            if (auto r = checkExpression(c.radius, name + " radius", false, label); !r) return r;
        } else {
            if (auto r = checkExpression(c.length, name + " length", false, label); !r) return r;
            if (auto r = checkExpression(c.width, name + " width", false, label); !r) return r;
            if (auto r = checkExpression(c.radius, name + " corner radius", true, label); !r) return r;
        }
    }
    return {};
}

core::Result<std::optional<domain::PartShape>> buildPartShape(const ContourSpec& spec, const domain::Part& part,
                                                              const parametric::Lookup& parameters,
                                                              const std::string& label) {
    if (auto r = checkContourSpec(spec, label); !r) return r.error();
    if (spec.kind == ContourKind::Rectangle && spec.cutouts.empty()) return std::optional<domain::PartShape>{};
    const Evaluator ev{parameters, part.length, part.width, part.thickness, label};

    core::Result<Outline> outline = Outline{};
    switch (spec.kind) {
    case ContourKind::Rectangle: {
        Outline o;
        o.contour = geometry::rectangleContour(part.length, part.width);
        o.bands = {part.edge(EdgeSide::L1), part.edge(EdgeSide::W2), part.edge(EdgeSide::L2), part.edge(EdgeSide::W1)};
        outline = std::move(o);
        break;
    }
    case ContourKind::RoundedCorners: outline = roundedOutline(spec, ev, part); break;
    case ContourKind::ArcEdge: outline = arcEdgeOutline(spec, ev, part); break;
    case ContourKind::Custom: outline = customOutline(spec, ev, part); break;
    }
    if (!outline) return outline.error();
    Outline& o = outline.value();
    if (auto v = geometry::validateContour(o.contour); !v)
        return invalid(label, "the outline is not a valid contour: " + v.error().message);

    domain::PartShape shape;
    shape.outer = std::move(o.contour);
    for (std::size_t i = 0; i < spec.cutouts.size(); ++i) {
        const std::string name = cutoutName(i, spec.cutouts[i]);
        auto c = buildCutout(spec.cutouts[i], name, ev);
        if (!c) return c.error();
        if (auto v = geometry::validateContour(c.value()); !v)
            return invalid(label, name + " is not a valid contour: " + v.error().message);
        const std::string where = name + " at " + describeCutout(c.value());
        for (const auto& vx : c.value().vertices)
            if (geometry::locatePoint(shape.outer, vx.point) != geometry::PointLocation::Inside)
                return invalid(label, where + " is not inside the part's outline (0 .. " + num(part.length) + " x 0 .. " +
                                          num(part.width) + " mm, corners and arcs removed).");
        if (segmentsTouch(c.value(), shape.outer))
            return invalid(label, where + " touches or crosses the part's outline; a cut-out must lie strictly inside.");
        for (std::size_t j = 0; j < shape.cutouts.size(); ++j) {
            const Contour& other = shape.cutouts[j];
            if (segmentsTouch(c.value(), other) ||
                geometry::locatePoint(other, c.value().vertices[0].point) != geometry::PointLocation::Outside ||
                geometry::locatePoint(c.value(), other.vertices[0].point) != geometry::PointLocation::Outside)
                return invalid(label, where + " overlaps or touches " + cutoutName(j, spec.cutouts[j]) + " at " +
                                          describeCutout(other) + ".");
        }
        shape.cutouts.push_back(std::move(c.value()));
    }
    bool anyBand = false;
    for (const auto& b : o.bands) anyBand = anyBand || b.has_value();
    if (anyBand) shape.outerEdges = std::move(o.bands);
    return std::optional<domain::PartShape>(std::move(shape));
}

core::Result<void> applyContour(domain::Part& part, const ContourSpec& spec, const parametric::Lookup& parameters,
                                const std::string& label) {
    auto shape = buildPartShape(spec, part, parameters, label);
    if (!shape) return shape.error();
    if (shape.value()) {
        part.shape = std::move(*shape.value());
        part.edges = {};
    }
    return {};
}

} // namespace fos::furniture
