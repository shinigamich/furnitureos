// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/hlr.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace fos::drawings {
namespace {

constexpr double kEps = core::kLengthEps;

struct RawPiece {
    Vec2 a, b;
    bool visible = true;
    double depth = 0;
    std::size_t order = 0;
    const std::string* source = nullptr;
};

/// Piece expressed on its carrier line: p = d * t + n * c.
struct LinePiece {
    double t0 = 0, t1 = 0;
    bool visible = true;
    double depth = 0;
    std::size_t order = 0;
    const std::string* source = nullptr;
};

using LineKey = std::tuple<long long, long long, long long>;

struct LineGroup {
    Vec2 d;
    double c = 0;
    std::vector<LinePiece> pieces;
};

/// Splits one edge into visible/hidden pieces against all boxes.
void classifyEdge(Vec2 a, Vec2 b, double depth, std::size_t order, const std::string& source,
                  const std::vector<HlrBox>& boxes, std::vector<RawPiece>& out) {
    const double len = length(b - a);
    if (len <= kEps) return;
    std::vector<std::pair<double, double>> hidden;
    for (const HlrBox& box : boxes) {
        if (box.depthNear <= depth + kEps) continue; // not in front of this edge
        const Segment2 seg{a, b};
        if (!segmentCrossesRect(seg, box.rect, kEps)) continue;
        if (const auto t = clipSegmentParams(seg, box.rect)) hidden.push_back(*t);
    }
    std::sort(hidden.begin(), hidden.end());
    std::vector<std::pair<double, double>> merged;
    const double tEps = kEps / len;
    for (const auto& h : hidden) {
        if (!merged.empty() && h.first <= merged.back().second + tEps)
            merged.back().second = std::max(merged.back().second, h.second);
        else
            merged.push_back(h);
    }
    const Vec2 d = b - a;
    const auto emit = [&](double t0, double t1, bool visible) {
        if ((t1 - t0) * len <= kEps) return;
        out.push_back({a + d * t0, a + d * t1, visible, depth, order, &source});
    };
    double cursor = 0.0;
    for (const auto& h : merged) {
        emit(cursor, h.first, true);
        emit(h.first, h.second, false);
        cursor = h.second;
    }
    emit(cursor, 1.0, true);
}

void addToGroups(const RawPiece& p, std::map<LineKey, LineGroup>& groups) {
    Vec2 d = p.b - p.a;
    const double len = length(d);
    if (len <= kEps) return;
    d = d * (1.0 / len);
    if (d.x < -1e-12 || (std::fabs(d.x) <= 1e-12 && d.y < 0)) d = -d;
    const Vec2 n{-d.y, d.x};
    const double c = dot(n, p.a);
    const LineKey key{std::llround(d.x * 1e8), std::llround(d.y * 1e8), std::llround(c * 1e5)};
    auto [it, inserted] = groups.try_emplace(key);
    if (inserted) {
        it->second.d = d;
        it->second.c = c;
    }
    double t0 = dot(d, p.a), t1 = dot(d, p.b);
    if (t0 > t1) std::swap(t0, t1);
    it->second.pieces.push_back({t0, t1, p.visible, p.depth, p.order, p.source});
}

const LinePiece* bestCovering(const std::vector<LinePiece>& pieces, double mid, bool visible) {
    const LinePiece* best = nullptr;
    for (const LinePiece& p : pieces) {
        if (p.visible != visible || mid < p.t0 || mid > p.t1) continue;
        if (!best || p.depth > best->depth + kEps ||
            (std::fabs(p.depth - best->depth) <= kEps && p.order < best->order))
            best = &p;
    }
    return best;
}

void mergeGroup(const LineGroup& g, HiddenLineMode mode, std::vector<ViewSegment>& out) {
    std::vector<double> cuts;
    for (const LinePiece& p : g.pieces) {
        cuts.push_back(p.t0);
        cuts.push_back(p.t1);
    }
    std::sort(cuts.begin(), cuts.end());
    const Vec2 n{-g.d.y, g.d.x};
    const auto at = [&](double t) { return g.d * t + n * g.c; };

    bool open = false;
    double start = 0, end = 0;
    bool openVisible = true;
    const std::string* openSource = nullptr;
    const auto flush = [&] {
        if (open && (openVisible || mode == HiddenLineMode::Dashed))
            out.push_back({at(start), at(end), openVisible ? LineStyle::Visible : LineStyle::Hidden, *openSource});
        open = false;
    };
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const double s = cuts[i], e = cuts[i + 1];
        if (e - s <= kEps) continue;
        const double mid = (s + e) * 0.5;
        const LinePiece* best = bestCovering(g.pieces, mid, true);
        if (!best) best = bestCovering(g.pieces, mid, false);
        if (!best) {
            flush();
            continue;
        }
        if (open && openVisible == best->visible && *openSource == *best->source && s - end <= kEps) {
            end = e;
            continue;
        }
        flush();
        open = true;
        start = s;
        end = e;
        openVisible = best->visible;
        openSource = best->source;
    }
    flush();
}

double normDeg(double a) noexcept {
    a = std::fmod(a, 360.0);
    if (a < 0) a += 360.0;
    return a;
}

/// Angles (degrees, relative to the arc start, in (0, sweep)) where the circle crosses the
/// lines carrying the sides of `r`.
void circleRectCrossings(const HlrArc& arc, double sweep, const Rect2& r, std::vector<double>& out) {
    const auto add = [&](double x, double y) {
        const double rel = normDeg(core::radToDeg(std::atan2(y - arc.center.y, x - arc.center.x)) - arc.startDeg);
        if (rel > 1e-12 && rel < sweep - 1e-12) out.push_back(rel);
    };
    for (const double x : {r.min.x, r.max.x}) {
        const double dx = x - arc.center.x;
        const double h2 = arc.radius * arc.radius - dx * dx;
        if (h2 < 0) continue;
        const double h = std::sqrt(h2);
        add(x, arc.center.y + h);
        add(x, arc.center.y - h);
    }
    for (const double y : {r.min.y, r.max.y}) {
        const double dy = y - arc.center.y;
        const double h2 = arc.radius * arc.radius - dy * dy;
        if (h2 < 0) continue;
        const double h = std::sqrt(h2);
        add(arc.center.x + h, y);
        add(arc.center.x - h, y);
    }
}

} // namespace

std::vector<ViewArc> removeHiddenArcs(const std::vector<HlrBox>& boxes, const std::vector<HlrArc>& arcs,
                                      HiddenLineMode mode) {
    std::vector<ViewArc> out;
    for (const HlrArc& arc : arcs) {
        if (!(arc.radius > kEps)) continue;
        double sweep = std::fmod(arc.endDeg - arc.startDeg, 360.0);
        if (sweep <= 0) sweep += 360.0;
        std::vector<const HlrBox*> front;
        std::vector<double> cuts{0.0, sweep};
        for (const HlrBox& box : boxes) {
            if (box.depthNear <= arc.depth + kEps) continue;
            front.push_back(&box);
            circleRectCrossings(arc, sweep, box.rect, cuts);
        }
        std::sort(cuts.begin(), cuts.end());
        const auto at = [&](double rel) {
            const double t = core::degToRad(arc.startDeg + rel);
            return arc.center + Vec2{std::cos(t), std::sin(t)} * arc.radius;
        };
        bool open = false, openVisible = true;
        double start = 0, end = 0;
        const auto flush = [&] {
            if (open && (openVisible || mode == HiddenLineMode::Dashed))
                out.push_back({arc.center, arc.radius, normDeg(arc.startDeg + start), normDeg(arc.startDeg + end),
                               openVisible ? LineStyle::Visible : LineStyle::Hidden, arc.sourceId});
            open = false;
        };
        for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
            const double s = cuts[i], e = cuts[i + 1];
            if (core::degToRad(e - s) * arc.radius <= kEps) continue;
            const Vec2 mid = at((s + e) * 0.5);
            bool visible = true;
            for (const HlrBox* b : front)
                if (b->rect.contains(mid, -kEps)) {
                    visible = false;
                    break;
                }
            if (open && openVisible == visible) {
                end = e;
                continue;
            }
            flush();
            open = true;
            openVisible = visible;
            start = s;
            end = e;
        }
        flush();
    }
    return out;
}

std::vector<ViewSegment> removeHiddenLines(const std::vector<HlrBox>& boxes,
                                           const std::vector<HlrLooseEdge>& looseEdges, HiddenLineMode mode) {
    return removeHiddenLines(boxes, {}, looseEdges, mode);
}

std::vector<ViewSegment> removeHiddenLines(const std::vector<HlrBox>& boxes, const std::vector<HlrEdge>& edges,
                                           const std::vector<HlrLooseEdge>& looseEdges, HiddenLineMode mode) {
    std::vector<RawPiece> raw;
    std::size_t order = 0;
    for (const HlrBox& box : boxes) {
        if (!box.drawEdges) {
            ++order;
            continue;
        }
        const Vec2 c[4] = {box.rect.min, {box.rect.max.x, box.rect.min.y}, box.rect.max,
                           {box.rect.min.x, box.rect.max.y}};
        for (const double depth : {box.depthNear, box.depthFar})
            for (int i = 0; i < 4; ++i) classifyEdge(c[i], c[(i + 1) % 4], depth, order, box.sourceId, boxes, raw);
        ++order;
    }
    for (const HlrEdge& e : edges) {
        classifyEdge(e.a, e.b, e.depth, order, e.sourceId, boxes, raw);
        ++order;
    }
    for (const HlrLooseEdge& e : looseEdges) {
        if (length(e.b - e.a) > kEps) raw.push_back({e.a, e.b, true, e.depth, order, &e.sourceId});
        ++order;
    }
    std::map<LineKey, LineGroup> groups;
    for (const RawPiece& p : raw) addToGroups(p, groups);
    std::vector<ViewSegment> out;
    for (const auto& [key, group] : groups) mergeGroup(group, mode, out);
    return out;
}

} // namespace fos::drawings
