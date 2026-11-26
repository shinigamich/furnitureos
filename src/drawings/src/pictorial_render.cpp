// SPDX-License-Identifier: AGPL-3.0-only
// Rendering of pictorial views: part edges, balloons, sheet placement and model-space export.
#include "fos/drawings/pictorial.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace fos::drawings {
namespace {

std::string balloonId(const PictorialView& view, const PictorialPart& part) {
    return view.id + "/balloon[" + part.partId.str() + "]";
}

/// Distance along unit direction d from p (inside `box`) to the box boundary.
double exitDistance(const Rect2& box, Vec2 p, Vec2 d) noexcept {
    double s = std::numeric_limits<double>::infinity();
    if (d.x > 1e-12) s = std::min(s, (box.max.x - p.x) / d.x);
    if (d.x < -1e-12) s = std::min(s, (box.min.x - p.x) / d.x);
    if (d.y > 1e-12) s = std::min(s, (box.max.y - p.y) / d.y);
    if (d.y < -1e-12) s = std::min(s, (box.min.y - p.y) / d.y);
    return std::isfinite(s) ? std::max(s, 0.0) : 0.0;
}

void renderBalloons(const PictorialView& view, const ViewPlacement& pl, const BalloonStyle& st,
                    std::vector<Primitive>& out) {
    const Rect2 box{pl.toPaper(view.bounds.min), pl.toPaper(view.bounds.max)};
    const Vec2 centre = box.center();
    std::vector<Vec2> placed;
    for (const PictorialPart& part : view.parts) {
        if (!part.balloonAnchor) continue;
        const std::string id = balloonId(view, part);
        const Vec2 anchor = pl.toPaper(*part.balloonAnchor);
        Vec2 d = anchor - centre;
        const double len = length(d);
        d = len > 1e-9 ? d * (1.0 / len) : Vec2{1, 0};
        double along = exitDistance(box, anchor, d) + st.clearance + st.radius;
        const double step = 2.0 * st.radius + st.gap;
        for (int attempt = 0; attempt < 64; ++attempt) {
            const Vec2 q = anchor + d * along;
            const bool clash = std::any_of(placed.begin(), placed.end(),
                                           [&](Vec2 o) { return length(o - q) < step - 1e-9; });
            if (!clash) break;
            along += step;
        }
        const Vec2 q = anchor + d * along;
        placed.push_back(q);
        out.emplace_back(Line{anchor, q - d * st.radius, LineStyle::Dimension, defaultLineWeight(LineStyle::Dimension),
                              part.partId.str(), id});
        Marker dot;
        dot.kind = MarkerKind::Dot;
        dot.position = anchor;
        dot.size = st.dotSize;
        dot.sourceId = part.partId.str();
        dot.elementId = id;
        out.emplace_back(dot);
        out.emplace_back(Circle{q, st.radius, LineStyle::Dimension, defaultLineWeight(LineStyle::Dimension),
                                part.partId.str(), id});
        Text t;
        t.position = q;
        t.height = st.textHeight;
        t.hAlign = HAlign::Center;
        t.vAlign = VAlign::Middle;
        t.text = std::to_string(part.number);
        t.sourceId = part.partId.str();
        t.elementId = id;
        out.emplace_back(std::move(t));
    }
}

} // namespace

std::vector<Primitive> renderPictorialView(const PictorialView& view, const ViewPlacement& placement,
                                           const PictorialRenderOptions& options) {
    std::vector<Primitive> out;
    out.reserve(view.segments.size() + (options.balloons ? 4 * view.parts.size() : 0));
    for (const ViewSegment& s : view.segments)
        out.emplace_back(
            Line{placement.toPaper(s.a), placement.toPaper(s.b), s.style, defaultLineWeight(s.style), s.sourceId, {}});
    if (options.balloons) renderBalloons(view, placement, options.balloon, out);
    return out;
}

std::vector<Primitive> pictorialModelSpacePrimitives(const PictorialView& view, const PictorialRenderOptions& options,
                                                     double annotationScale) {
    PictorialRenderOptions scaledOptions = options;
    BalloonStyle& b = scaledOptions.balloon;
    b.radius *= annotationScale;
    b.textHeight *= annotationScale;
    b.clearance *= annotationScale;
    b.gap *= annotationScale;
    b.dotSize *= annotationScale;
    return renderPictorialView(view, ViewPlacement{{0, 0}, 1.0}, scaledOptions);
}

core::Result<PictorialDrawingResult> generatePictorialDrawing(const domain::Model& model,
                                                              const domain::AssemblyId& assemblyId,
                                                              const PictorialDrawingOptions& options) {
    const domain::Assembly* assembly = model.findAssembly(assemblyId);
    if (!assembly)
        return core::makeError(core::ErrorCode::NotFound,
                               "Assembly '" + assemblyId.str() + "' does not exist in the model.", assemblyId.str());
    PictorialViewOptions viewOptions = options.view;
    viewOptions.firstPartNumber = firstPartListNumber(model, assemblyId);
    auto viewResult = generatePictorialView(*assembly, viewOptions);
    if (!viewResult) return viewResult.error();
    PictorialDrawingResult result;
    result.view = std::move(viewResult).value();
    result.diagnostics = result.view.diagnostics;
    if (options.render.balloons)
        for (const PictorialPart& p : result.view.parts)
            if (!p.balloonAnchor)
                result.diagnostics.push_back(
                    {core::Severity::Warning, "drawings.pictorial.balloon_hidden",
                     "Part '" + (p.name.empty() ? p.partId.str() : p.name) + "' (no. " + std::to_string(p.number) +
                         ") is completely hidden in the " + toString(result.view.kind) +
                         " view, so it has no balloon; it is still listed in the part list.",
                     p.partId.str()});

    SheetOptions sheetOptions = options.sheet;
    if (sheetOptions.title.title.empty()) sheetOptions.title.title = assembly->name;
    const ScaleRequest& req = sheetOptions.scale;
    std::vector<Scale> candidates;
    if (req.mode == ScaleRequest::Mode::Explicit) {
        if (!(req.scale.numerator > 0 && req.scale.denominator > 0))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Drawing scale " + req.scale.text() + " is not valid; both numbers must be positive.");
        candidates.push_back(req.scale);
    } else {
        candidates = standardScales();
    }
    const std::string sheetId = assemblyId.str() + "/sheet." + toString(result.view.kind);
    std::optional<core::Error> lastError;
    for (const Scale& scale : candidates) {
        auto sheetResult = makeSheet(sheetOptions, scale, sheetId);
        if (!sheetResult) return sheetResult.error();
        Sheet sheet = std::move(sheetResult).value();
        std::vector<Primitive> prims = renderPictorialView(result.view, {{0, 0}, scale.factor()}, options.render);
        Rect2 box = boundsOf(prims);
        box.expand(Rect2{result.view.bounds.min * scale.factor(), result.view.bounds.max * scale.factor()});
        const Rect2& area = sheet.drawingArea;
        if (box.width() > area.width() + 1e-6 || box.height() > area.height() + 1e-6) {
            lastError = core::makeError(
                core::ErrorCode::ConstraintViolation,
                (req.mode == ScaleRequest::Mode::Explicit ? "At scale " + scale.text() + " "
                                                          : "Even at the smallest standard scale " + scale.text() + " ") +
                    "the " + toString(result.view.kind) + " view of '" + assembly->name + "' needs " +
                    core::formatLength(box.width()) + " x " + core::formatLength(box.height()) +
                    " mm, but the drawing area of the " + core::formatLength(sheet.width) + " x " +
                    core::formatLength(sheet.height) + " mm sheet is only " + core::formatLength(area.width()) + " x " +
                    core::formatLength(area.height()) + " mm. Choose a smaller scale, a larger sheet or fit-to-page.",
                sheetId);
            continue;
        }
        const Vec2 shift = area.center() - box.center();
        for (Primitive& p : prims) {
            translate(p, shift);
            sheet.primitives.push_back(std::move(p));
        }
        sheet.views.push_back({result.view.id, {shift, scale.factor()}, box.translated(shift)});
        result.sheet = std::move(sheet);
        return result;
    }
    return *lastError;
}

} // namespace fos::drawings
