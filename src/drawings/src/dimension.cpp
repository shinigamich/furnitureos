// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/annotation.hpp"

#include <algorithm>
#include <cmath>

namespace fos::drawings {

double measuredValue(const Dimension& d) noexcept {
    return d.orientation == DimensionOrientation::Horizontal ? std::fabs(d.p2.x - d.p1.x)
                                                             : std::fabs(d.p2.y - d.p1.y);
}

std::string dimensionText(const Dimension& d, int decimals) {
    return d.textOverride ? *d.textOverride : core::formatLength(measuredValue(d), decimals);
}

DimensionGeometry layoutDimension(const Dimension& d, const ViewPlacement& placement, const DimensionStyle& style) {
    DimensionGeometry g;
    const Vec2 a = placement.toPaper(d.p1), b = placement.toPaper(d.p2);
    const bool horizontal = d.orientation == DimensionOrientation::Horizontal;
    // Work in (along, across) coordinates: along = measured direction.
    const auto along = [&](Vec2 p) { return horizontal ? p.x : p.y; };
    const auto across = [&](Vec2 p) { return horizontal ? p.y : p.x; };
    const auto make = [&](double al, double ac) { return horizontal ? Vec2{al, ac} : Vec2{ac, al}; };

    const double ref = d.offset >= 0 ? std::max(across(a), across(b)) : std::min(across(a), across(b));
    const double linePos = ref + d.offset;
    g.line = {make(along(a), linePos), make(along(b), linePos)};

    const auto extension = [&](Vec2 p) -> std::optional<Segment2> {
        const double delta = linePos - across(p);
        if (std::fabs(delta) <= style.extensionGap) return std::nullopt;
        const double s = delta > 0 ? 1.0 : -1.0;
        return Segment2{make(along(p), across(p) + s * style.extensionGap),
                        make(along(p), linePos + s * style.extensionOvershoot)};
    };
    g.ext1 = extension(a);
    g.ext2 = extension(b);

    const double span = std::fabs(along(b) - along(a));
    g.terminatorsOutside = style.terminator == DimensionTerminator::Arrow && span < 2.5 * style.arrowSize;

    // Text: above horizontal lines (+y), to the left of vertical lines (-x, rotated 90 deg).
    const double rowStep = style.textHeight + style.textGap;
    const double textDistance = style.textGap + static_cast<double>(std::max(d.textRow, 0)) * rowStep;
    const double mid = (along(a) + along(b)) * 0.5;
    g.text.position = (horizontal ? Vec2{mid, linePos + textDistance} : Vec2{linePos - textDistance, mid}) + d.textShift;
    g.text.height = style.textHeight;
    g.text.hAlign = HAlign::Center;
    g.text.vAlign = VAlign::Baseline;
    g.text.rotationDeg = horizontal ? 0.0 : 90.0;
    g.text.text = dimensionText(d, style.decimals);
    g.text.elementId = d.id;
    g.textBounds = textBox(g.text);
    return g;
}

std::vector<double> stackDimensionOffsets(const std::vector<int>& rows, DimensionOrientation orientation, int side,
                                          const DimensionStyle& style) {
    // Text direction along the stacking axis: +1 for horizontal dimensions (above the
    // line), -1 for vertical ones (left of the line).
    const int textDir = orientation == DimensionOrientation::Horizontal ? 1 : -1;
    const bool textOnViewSide = textDir != (side >= 0 ? 1 : -1);
    const double sign = side >= 0 ? 1.0 : -1.0;
    std::vector<double> offsets;
    double cursor = style.firstGap;
    for (const int r : rows) {
        const double depth = static_cast<double>(std::max(r, 1)) * (style.textHeight + style.textGap);
        double line = cursor;
        if (textOnViewSide) {
            line = cursor + depth;
            cursor = line + style.stackGap;
        } else {
            cursor = line + depth + style.stackGap;
        }
        offsets.push_back(sign * line);
    }
    return offsets;
}

namespace {

struct Boundary {
    double coord = 0;
    std::string label;
};

const char* axisName(Vec3 axis) noexcept {
    if (std::fabs(axis.x) > 0.5) return "width";
    if (std::fabs(axis.y) > 0.5) return "depth";
    return "height";
}

/// Sorted, de-duplicated boundaries along one view axis.
std::vector<Boundary> chainBoundaries(const View& view, bool alongU) {
    std::vector<Boundary> bs;
    bs.push_back({alongU ? view.bounds.min.x : view.bounds.min.y, "env:min"});
    bs.push_back({alongU ? view.bounds.max.x : view.bounds.max.y, "env:max"});
    const PanelAspect wanted = alongU ? PanelAspect::EdgeOnVertical : PanelAspect::EdgeOnHorizontal;
    for (const PartFootprint& fp : view.footprints) {
        if (fp.aspect != wanted) continue;
        bs.push_back({alongU ? fp.rect.min.x : fp.rect.min.y, fp.partId + ":min"});
        bs.push_back({alongU ? fp.rect.max.x : fp.rect.max.y, fp.partId + ":max"});
    }
    // Stable sort keeps "env" first and then assembly order among equal coordinates.
    std::stable_sort(bs.begin(), bs.end(), [](const Boundary& l, const Boundary& r) { return l.coord < r.coord; });
    std::vector<Boundary> unique;
    for (const Boundary& b : bs) {
        if (!unique.empty() && b.coord - unique.back().coord <= core::kLengthEps) {
            // Prefer the envelope label when it coincides with a part face.
            if (b.label.rfind("env:", 0) == 0) unique.back().label = b.label;
            continue;
        }
        unique.push_back(b);
    }
    return unique;
}

/// Greedy row assignment: each text goes to the lowest row where its estimated box
/// (plus 0.5 mm margin each side) does not overlap a text already placed in that row.
void assignRows(std::vector<Dimension>& chain, bool alongU, double scale, const DimensionStyle& style) {
    std::vector<std::vector<std::pair<double, double>>> rows;
    for (Dimension& d : chain) {
        const double c0 = (alongU ? d.p1.x : d.p1.y) * scale, c1 = (alongU ? d.p2.x : d.p2.y) * scale;
        const double half = estimateTextWidth(dimensionText(d, style.decimals), style.textHeight) * 0.5 + 0.5;
        const double lo = (c0 + c1) * 0.5 - half, hi = (c0 + c1) * 0.5 + half;
        std::size_t row = 0;
        for (;; ++row) {
            if (row == rows.size()) rows.emplace_back();
            bool clash = false;
            for (const auto& [l, h] : rows[row]) clash = clash || (lo < h && l < hi);
            if (!clash) break;
        }
        rows[row].emplace_back(lo, hi);
        d.textRow = static_cast<int>(row);
    }
}

void addStack(const View& view, bool alongU, double scale, const DimensionStyle& style,
              const AutoDimensionOptions& options, std::vector<Dimension>& out) {
    const DimensionOrientation orient = alongU ? DimensionOrientation::Horizontal : DimensionOrientation::Vertical;
    const auto anchor = [&](double coord) {
        return alongU ? Vec2{coord, view.bounds.min.y} : Vec2{view.bounds.min.x, coord};
    };
    std::vector<std::vector<Dimension>> entries;
    if (options.chains) {
        const std::vector<Boundary> bs = chainBoundaries(view, alongU);
        if (bs.size() > 2) {
            std::vector<Dimension> chain;
            for (std::size_t i = 0; i + 1 < bs.size(); ++i) {
                Dimension d;
                d.id = view.id + "/dim.chain." + (alongU ? "h" : "v") + "[" + bs[i].label + ".." + bs[i + 1].label + "]";
                d.orientation = orient;
                d.p1 = anchor(bs[i].coord);
                d.p2 = anchor(bs[i + 1].coord);
                chain.push_back(std::move(d));
            }
            assignRows(chain, alongU, scale, style);
            entries.push_back(std::move(chain));
        }
    }
    if (options.overall) {
        Dimension d;
        d.id = view.id + "/dim.overall." + axisName(alongU ? view.basis.right : view.basis.up);
        d.orientation = orient;
        d.p1 = anchor(alongU ? view.bounds.min.x : view.bounds.min.y);
        d.p2 = anchor(alongU ? view.bounds.max.x : view.bounds.max.y);
        entries.push_back({std::move(d)});
    }
    std::vector<int> rows;
    for (const auto& e : entries) {
        int r = 1;
        for (const Dimension& d : e) r = std::max(r, d.textRow + 1);
        rows.push_back(r);
    }
    const std::vector<double> offsets = stackDimensionOffsets(rows, orient, -1, style);
    for (std::size_t i = 0; i < entries.size(); ++i)
        for (Dimension& d : entries[i]) {
            d.offset = offsets[i];
            out.push_back(std::move(d));
        }
}

} // namespace

std::vector<Dimension> autoDimensionView(const View& view, double scale, const DimensionStyle& style,
                                         const AutoDimensionOptions& options) {
    std::vector<Dimension> out;
    if (view.bounds.empty() || !(scale > 0)) return out;
    addStack(view, true, scale, style, options, out);
    addStack(view, false, scale, style, options, out);
    return out;
}

} // namespace fos::drawings
