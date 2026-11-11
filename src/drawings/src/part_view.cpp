// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/part_view.hpp"

#include <algorithm>
#include <array>
#include <variant>
#include <cmath>

namespace fos::drawings {
namespace {

using domain::Face;

bool isMainFace(Face f) noexcept { return f == Face::A || f == Face::B; }

struct Builder {
    const domain::Part& part;
    PartViewFace viewFace;
    View& view;
    double ySign;

    Vec2 map(double x, double y) const noexcept { return {x, ySign * y}; }
    bool onViewedFace(Face f) const noexcept {
        return (f == Face::A) == (viewFace == PartViewFace::A);
    }
    void seg(Vec2 a, Vec2 b, LineStyle s, const std::string& src) { view.segments.push_back({a, b, s, src}); }
    void rect(double x0, double y0, double x1, double y1, LineStyle s, const std::string& src) {
        const Vec2 c[4] = {map(x0, y0), map(x1, y0), map(x1, y1), map(x0, y1)};
        for (int i = 0; i < 4; ++i) seg(c[i], c[(i + 1) % 4], s, src);
    }
    void warn(const char* code, const domain::Operation& op, const std::string& what) {
        view.diagnostics.push_back({core::Severity::Warning, code,
                                    "Operation '" + op.id.str() + "' on face " + domain::toString(domain::faceOf(op)) +
                                        " of '" + part.name + "' " + what,
                                    op.id.str()});
    }

    /// Box [along0, along1] x [0, depth] entering from an edge face, in part coordinates.
    /// Returns {x0, y0, x1, y1}.
    std::array<double, 4> edgeBox(Face f, double along0, double along1, double depth) const noexcept {
        const double L = part.length, W = part.width;
        switch (f) {
        case Face::L1: return {along0, 0.0, along1, std::min(depth, W)};
        case Face::L2: return {along0, std::max(W - depth, 0.0), along1, W};
        case Face::W1: return {0.0, along0, std::min(depth, L), along1};
        default: return {std::max(L - depth, 0.0), along0, L, along1};
        }
    }

    void operator()(const domain::Hole& h, const domain::Operation& op) {
        const std::string& id = op.id.str();
        if (!(h.diameter > 0)) return warn("drawings.part.invalid_operation", op, "has a non-positive diameter and was not drawn.");
        const double r = h.diameter * 0.5;
        if (isMainFace(h.face)) {
            const Vec2 c = map(h.u, h.v);
            const LineStyle style = (h.through || onViewedFace(h.face)) ? LineStyle::Visible : LineStyle::Hidden;
            view.circles.push_back({c, r, style, id});
            const double e = r * 1.5;
            seg(c - Vec2{e, 0}, c + Vec2{e, 0}, LineStyle::Center, id);
            seg(c - Vec2{0, e}, c + Vec2{0, e}, LineStyle::Center, id);
            if (h.through) {
                const double k = r * std::sqrt(0.5);
                seg(c - Vec2{k, k}, c + Vec2{k, k}, LineStyle::Thin, id);
                seg(c - Vec2{k, -k}, c + Vec2{k, -k}, LineStyle::Thin, id);
            }
            return;
        }
        const bool alongLength = h.face == Face::L1 || h.face == Face::L2;
        const double full = alongLength ? part.width : part.length;
        const double depth = h.through ? full : h.depth;
        if (!(depth > 0)) return warn("drawings.part.invalid_operation", op, "has a non-positive depth and was not drawn.");
        const auto b = edgeBox(h.face, h.u - r, h.u + r, depth);
        rect(b[0], b[1], b[2], b[3], LineStyle::Hidden, id);
        // Axis of the bore, extended 1 mm + r past its bottom.
        const double ext = r + 1.0;
        if (alongLength)
            seg(map(h.u, b[1] - (h.face == Face::L2 ? ext : 0)), map(h.u, b[3] + (h.face == Face::L1 ? ext : 0)),
                LineStyle::Center, id);
        else
            seg(map(b[0] - (h.face == Face::W2 ? ext : 0), h.u), map(b[2] + (h.face == Face::W1 ? ext : 0), h.u),
                LineStyle::Center, id);
    }

    void operator()(const domain::Groove& g, const domain::Operation& op) {
        const std::string& id = op.id.str();
        const Vec2 p1{g.u1, g.v1}, p2{g.u2, g.v2};
        const double len = length(p2 - p1);
        if (!(len > core::kLengthEps) || !(g.width > 0))
            return warn("drawings.part.invalid_operation", op, "has zero length or width and was not drawn.");
        const Vec2 d = (p2 - p1) * (1.0 / len);
        const Vec2 n = Vec2{-d.y, d.x} * (g.width * 0.5);
        const Vec2 c[4] = {p1 + n, p2 + n, p2 - n, p1 - n};
        if (isMainFace(g.face)) {
            const LineStyle style = onViewedFace(g.face) ? LineStyle::Visible : LineStyle::Hidden;
            for (int i = 0; i < 4; ++i)
                seg(map(c[i].x, c[i].y), map(c[(i + 1) % 4].x, c[(i + 1) % 4].y), style, id);
            return;
        }
        double lo = c[0].x, hi = c[0].x;
        for (const Vec2& q : c) {
            lo = std::min(lo, q.x);
            hi = std::max(hi, q.x);
        }
        const auto b = edgeBox(g.face, lo, hi, g.depth);
        rect(b[0], b[1], b[2], b[3], LineStyle::Hidden, id);
    }

    void operator()(const domain::Pocket& p, const domain::Operation& op) {
        const std::string& id = op.id.str();
        if (!isMainFace(p.face))
            return warn("drawings.part.unsupported_operation", op,
                        "is a pocket on an edge face; such pockets are not shown in part drawings.");
        if (p.contour.size() < 3)
            return warn("drawings.part.invalid_operation", op, "has fewer than 3 contour points and was not drawn.");
        const LineStyle style = (p.through || onViewedFace(p.face)) ? LineStyle::Visible : LineStyle::Hidden;
        for (std::size_t i = 0; i < p.contour.size(); ++i) {
            const Vec2 a = p.contour[i], b = p.contour[(i + 1) % p.contour.size()];
            seg(map(a.x, a.y), map(b.x, b.y), style, id);
        }
    }
};

std::string sizeText(std::initializer_list<double> values) {
    std::string s;
    for (const double v : values) {
        if (!s.empty()) s += " \xC3\x97 "; // U+00D7 multiplication sign
        s += core::formatLength(v);
    }
    return s;
}

struct Coord {
    double value = 0;
    std::string opId;
};

void addCoord(std::vector<Coord>& list, double value, const std::string& opId, double limit) {
    if (value <= core::kLengthEps || value >= limit - core::kLengthEps) return;
    for (const Coord& c : list)
        if (std::fabs(c.value - value) <= core::kLengthEps) return;
    list.push_back({value, opId});
}

} // namespace

View generatePartView(const domain::Part& part, const PartViewOptions& options) {
    View view;
    view.subject = ViewSubject::Part;
    view.kind = ViewKind::Top;
    const bool faceA = options.face == PartViewFace::A;
    view.basis = faceA ? ViewBasis{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}} : ViewBasis{{1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    view.id = part.id.str() + (faceA ? "/view.faceA" : "/view.faceB");
    if (!(part.length > 0 && part.width > 0 && part.thickness > 0)) {
        view.diagnostics.push_back({core::Severity::Error, "drawings.part.degenerate",
                                    "Part '" + part.name + "' has a non-positive size (" +
                                        core::formatLength(part.length) + " x " + core::formatLength(part.width) +
                                        " x " + core::formatLength(part.thickness) + " mm) and cannot be drawn.",
                                    part.id.str()});
        return view;
    }
    Builder b{part, options.face, view, faceA ? 1.0 : -1.0};
    b.rect(0, 0, part.length, part.width, LineStyle::Visible, part.id.str());
    view.bounds = Rect2::fromCorners(b.map(0, 0), b.map(part.length, part.width));
    PartFootprint fp;
    fp.partId = part.id.str();
    fp.role = part.role;
    fp.rect = view.bounds;
    fp.depthNear = faceA ? part.thickness : 0.0;
    fp.depthFar = faceA ? 0.0 : -part.thickness;
    view.footprints.push_back(fp);
    for (const domain::Operation& op : part.operations)
        std::visit([&](const auto& shape) { b(shape, op); }, op.shape);
    return view;
}

ViewAnnotations annotatePartView(const domain::Model& model, const domain::Part& part, const View& view,
                                 double scale, const DimensionStyle& style, const PartViewOptions& options) {
    ViewAnnotations ann;
    if (view.bounds.empty() || !(scale > 0)) return ann;
    const PartDrawingLabels& lb = options.labels;
    const double ySign = options.face == PartViewFace::A ? 1.0 : -1.0;
    const auto map = [&](double x, double y) { return Vec2{x, ySign * y}; };
    const double L = part.length, W = part.width;

    // --- dimensions -------------------------------------------------------------
    std::vector<Coord> xs, ys;
    struct HoleGroup {
        Face face;
        double diameter, depth;
        bool through;
        int count;
    };
    std::vector<HoleGroup> groups;
    for (const domain::Operation& op : part.operations) {
        const auto* h = std::get_if<domain::Hole>(&op.shape);
        if (!h || !(h->diameter > 0)) continue;
        if (isMainFace(h->face)) {
            addCoord(xs, h->u, op.id.str(), L);
            addCoord(ys, h->v, op.id.str(), W);
        } else if (h->face == Face::L1 || h->face == Face::L2) {
            addCoord(xs, h->u, op.id.str(), L);
        } else {
            addCoord(ys, h->u, op.id.str(), W);
        }
        // Legend group and per-hole label.
        std::size_t gi = 0;
        for (; gi < groups.size(); ++gi) {
            const HoleGroup& g = groups[gi];
            if (g.face == h->face && std::fabs(g.diameter - h->diameter) <= core::kLengthEps &&
                g.through == h->through && (h->through || std::fabs(g.depth - h->depth) <= core::kLengthEps))
                break;
        }
        if (gi == groups.size()) groups.push_back({h->face, h->diameter, h->depth, h->through, 0});
        ++groups[gi].count;
        if (isMainFace(h->face)) {
            Note n;
            n.id = view.id + "/hole[" + op.id.str() + "].label";
            const double k = h->diameter * 0.5 * std::sqrt(0.5);
            n.anchor = map(h->u, h->v) + Vec2{k, k};
            n.paperOffset = {0.8, 0.8};
            n.text = "H" + std::to_string(gi + 1);
            n.height = options.noteHeight;
            n.sourceId = op.id.str();
            ann.notes.push_back(std::move(n));
        }
    }
    const auto byValue = [](const Coord& a, const Coord& b) { return a.value < b.value; };
    std::stable_sort(xs.begin(), xs.end(), byValue);
    std::stable_sort(ys.begin(), ys.end(), byValue);
    if (!options.holeDimensions) {
        xs.clear();
        ys.clear();
    }
    const auto stack = [&](const std::vector<Coord>& coords, bool horizontal) {
        const DimensionOrientation orient =
            horizontal ? DimensionOrientation::Horizontal : DimensionOrientation::Vertical;
        const auto anchor = [&](double c) {
            return horizontal ? Vec2{c, view.bounds.min.y} : Vec2{view.bounds.min.x, ySign * c};
        };
        std::vector<Dimension> dims;
        for (const Coord& c : coords) {
            Dimension d;
            d.id = view.id + "/dim.hole." + (horizontal ? "x" : "y") + "[" + c.opId + "]";
            d.orientation = orient;
            d.p1 = anchor(0);
            d.p2 = anchor(c.value);
            dims.push_back(std::move(d));
        }
        Dimension overall;
        overall.id = view.id + (horizontal ? "/dim.overall.length" : "/dim.overall.width");
        overall.orientation = orient;
        overall.p1 = anchor(0);
        overall.p2 = anchor(horizontal ? L : W);
        dims.push_back(std::move(overall));
        const std::vector<double> offsets = stackDimensionOffsets(std::vector<int>(dims.size(), 1), orient, -1, style);
        for (std::size_t i = 0; i < dims.size(); ++i) {
            dims[i].offset = offsets[i];
            ann.dimensions.push_back(std::move(dims[i]));
        }
    };
    stack(xs, true);
    stack(ys, false);

    // --- edge-band marks ----------------------------------------------------------
    std::vector<domain::EdgeBandId> bands;
    for (const domain::EdgeSide side : domain::kAllEdges) {
        const auto& band = part.edge(side);
        if (!band) continue;
        std::size_t bi = 0;
        while (bi < bands.size() && bands[bi] != *band) ++bi;
        if (bi == bands.size()) bands.push_back(*band);
        EdgeBandMark m;
        m.id = view.id + "/edge." + domain::toString(side);
        switch (side) {
        case domain::EdgeSide::L1: m.a = map(0, 0); m.b = map(L, 0); m.inward = {0, ySign}; break;
        case domain::EdgeSide::L2: m.a = map(0, W); m.b = map(L, W); m.inward = {0, -ySign}; break;
        case domain::EdgeSide::W1: m.a = map(0, 0); m.b = map(0, W); m.inward = {1, 0}; break;
        case domain::EdgeSide::W2: m.a = map(L, 0); m.b = map(L, W); m.inward = {-1, 0}; break;
        }
        m.code = "E" + std::to_string(bi + 1);
        m.sourceId = band->str();
        ann.edgeBandMarks.push_back(std::move(m));
    }

    // --- grain --------------------------------------------------------------------
    if (part.grain != domain::GrainDirection::None) {
        GrainArrow g;
        g.id = view.id + "/grain";
        g.center = view.bounds.center();
        g.direction = part.grain == domain::GrainDirection::AlongLength ? Vec2{1, 0} : Vec2{0, 1};
        ann.grainArrows.push_back(std::move(g));
    }

    // --- notes above the view, listed top to bottom -------------------------------
    struct NoteLine {
        std::string id, text;
        double height;
        std::string sourceId;
    };
    std::vector<NoteLine> lines;
    const std::string unit = " " + lb.unit;
    lines.push_back({view.id + "/title", part.name, options.titleHeight, part.id.str()});
    const domain::Material* mat = model.findMaterial(part.material);
    lines.push_back({view.id + "/note.material",
                     lb.material + ": " + (mat ? mat->name : part.material.str() + " (" + lb.missing + ")"),
                     options.noteHeight, part.material.str()});
    lines.push_back({view.id + "/note.finished",
                     lb.finishedSize + ": " + sizeText({L, W, part.thickness}) + unit + "   " + lb.quantity + ": " +
                         std::to_string(options.quantity),
                     options.noteHeight, part.id.str()});
    const domain::CutSize cut = domain::cutSize(part, model);
    lines.push_back({view.id + "/note.cutsize", lb.cutSize + ": " + sizeText({cut.length, cut.width}) + unit,
                     options.noteHeight, part.id.str()});
    if (part.grain != domain::GrainDirection::None)
        lines.push_back({view.id + "/note.grain",
                         lb.grain + ": " +
                             (part.grain == domain::GrainDirection::AlongLength ? lb.grainAlongLength
                                                                                : lb.grainAlongWidth),
                         options.noteHeight, part.id.str()});
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const domain::EdgeBand* eb = model.findEdgeBand(bands[i]);
        std::string text = "E" + std::to_string(i + 1) + " = " + lb.edgeBand + " ";
        text += eb ? eb->name + " (" + sizeText({eb->thickness, eb->width}) + unit + ")"
                   : bands[i].str() + " (" + lb.missing + ")";
        std::string edges;
        for (const domain::EdgeSide side : domain::kAllEdges)
            if (part.edge(side) && *part.edge(side) == bands[i])
                edges += std::string(edges.empty() ? "" : ", ") + domain::toString(side);
        lines.push_back({view.id + "/legend.edge[" + bands[i].str() + "]", text + ": " + edges, options.noteHeight,
                         bands[i].str()});
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const HoleGroup& g = groups[i];
        std::string text = "H" + std::to_string(i + 1) + " = " + lb.hole + " \xC3\x98" + core::formatLength(g.diameter);
        text += g.through ? " " + lb.through : " \xC3\x97 " + core::formatLength(g.depth) + " " + lb.deep;
        text += std::string(", ") + lb.face + " " + domain::toString(g.face) + ", " + std::to_string(g.count) + "\xC3\x97";
        lines.push_back({view.id + "/legend.hole.H" + std::to_string(i + 1), text, options.noteHeight, {}});
    }
    // Stack upwards from 4 mm above the outline; each line takes 1.5 x its height.
    double y = 4.0;
    for (std::size_t i = lines.size(); i-- > 0;) {
        Note n;
        n.id = lines[i].id;
        n.anchor = {view.bounds.min.x, view.bounds.max.y};
        n.paperOffset = {0.0, y};
        n.text = lines[i].text;
        n.height = lines[i].height;
        n.sourceId = lines[i].sourceId;
        y += lines[i].height * 1.5;
        ann.notes.push_back(std::move(n));
    }
    return ann;
}

} // namespace fos::drawings
