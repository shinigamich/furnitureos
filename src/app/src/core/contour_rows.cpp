// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/contour_rows.hpp"

#include "fos/app/cabinet_parts.hpp"
#include "fos/app/parameter_rows.hpp"
#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "fos/domain/model.hpp"
#include "fos/furniture/contour.hpp"
#include "fos/geometry/contour.hpp"

#include <QCoreApplication>

#include <cmath>

namespace fos::app {

namespace {
class ContourText {
    Q_DECLARE_TR_FUNCTIONS(ContourText)
};

QString fromStd(const std::string& s) { return QString::fromStdString(s); }

/// Plain numbers normalised ("12,5 mm" -> "12.5"); everything else trimmed.
std::string expressionText(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return {};
    if (const auto v = parsePlainNumber(t)) return core::formatLength(*v, 6);
    return t.toStdString();
}

QString bulgeText(double bulge) {
    return bulge == 0.0 ? QString() : fromStd(core::formatLength(bulge, 6));
}
} // namespace

ContourInput contourInput(const std::optional<furniture::ContourSpec>& spec) {
    ContourInput in;
    if (!spec) return in;
    in.kind = spec->kind;
    for (std::size_t i = 0; i < 4; ++i) in.radii[i] = fromStd(spec->radii[i]);
    in.arcEdge = spec->arcEdge;
    in.arcRise = fromStd(spec->arcRise);
    in.arcDirection = spec->arcDirection;
    for (const auto& v : spec->vertices) in.vertices.push_back({fromStd(v.x), fromStd(v.y), bulgeText(v.bulge), v.bandFrom});
    for (const auto& c : spec->cutouts)
        in.cutouts.push_back({c.kind, fromStd(c.x), fromStd(c.y), fromStd(c.length), fromStd(c.width), fromStd(c.radius)});
    return in;
}

core::Result<std::optional<furniture::ContourSpec>> contourSpec(const ContourInput& in) {
    using K = furniture::ContourKind;
    furniture::ContourSpec spec;
    spec.kind = in.kind;
    switch (in.kind) {
    case K::Rectangle:
        break;
    case K::RoundedCorners:
        for (std::size_t i = 0; i < 4; ++i) spec.radii[i] = expressionText(in.radii[i]);
        break;
    case K::ArcEdge:
        spec.arcEdge = in.arcEdge;
        spec.arcRise = expressionText(in.arcRise);
        spec.arcDirection = in.arcDirection;
        break;
    case K::Custom:
        for (std::size_t i = 0; i < in.vertices.size(); ++i) {
            const VertexRow& row = in.vertices[i];
            furniture::ContourVertexSpec v{expressionText(row.x), expressionText(row.y), 0.0, row.bandFrom};
            const QString b = row.bulge.trimmed();
            if (!b.isEmpty()) {
                const auto value = parsePlainNumber(b);
                if (!value || !std::isfinite(*value))
                    return core::makeError(core::ErrorCode::InvalidArgument,
                                           ContourText::tr("Vertex %1: the bulge “%2” is not a number (0 = straight, "
                                                           "1 = half circle, negative = clockwise).")
                                               .arg(i + 1)
                                               .arg(b)
                                               .toStdString());
                v.bulge = *value;
            }
            spec.vertices.push_back(std::move(v));
        }
        break;
    }
    for (const CutoutRow& row : in.cutouts) {
        furniture::CutoutSpec c;
        c.kind = row.kind;
        c.x = expressionText(row.x);
        c.y = expressionText(row.y);
        c.radius = expressionText(row.radius);
        if (row.kind == furniture::CutoutKind::Rectangle) {
            c.length = expressionText(row.length);
            c.width = expressionText(row.width);
        }
        spec.cutouts.push_back(std::move(c));
    }
    if (spec.kind == K::Rectangle && spec.cutouts.empty()) return std::optional<furniture::ContourSpec>{};
    return std::optional<furniture::ContourSpec>(std::move(spec));
}

std::vector<VertexRow> rectangleVertexRows() {
    const QString L = QString::fromLatin1(furniture::contour_param::kLength);
    const QString W = QString::fromLatin1(furniture::contour_param::kWidth);
    const QString zero = QStringLiteral("0");
    return {{zero, zero, {}, {}}, {L, zero, {}, {}}, {L, W, {}, {}}, {zero, W, {}, {}}};
}

std::optional<ContourTarget> contourTarget(const project::Project& project, const std::string& itemId,
                                           const std::string& partId) {
    const project::Item* item = project.findItem(itemId);
    if (!item) return std::nullopt;
    if (item->cabinet()) {
        const auto slot = partSlot(itemId, partId);
        if (!slot) return std::nullopt;
        return ContourTarget{itemId, *slot};
    }
    if (!partId.empty() && partId != item->panel()->panel.id.str()) return std::nullopt;
    return ContourTarget{itemId, {}};
}

std::optional<furniture::ContourSpec> currentContour(const project::Project& project, const ContourTarget& target) {
    const project::Item* item = project.findItem(target.itemId);
    if (!item) return std::nullopt;
    if (const auto* cabinet = item->cabinet()) return currentOverride(*cabinet, target.slot).contour;
    return item->panel()->panel.contour;
}

project::Command contourCommand(const ContourTarget& target, std::optional<furniture::ContourSpec> spec) {
    if (target.slot.empty()) return project::commands::setPanelContour(target.itemId, std::move(spec));
    return project::commands::setPartContour(target.itemId, target.slot, std::move(spec));
}

core::Result<void> applyContourInput(Session& session, const ContourTarget& target, const ContourInput& input) {
    auto spec = contourSpec(input);
    if (!spec) return spec.error();
    return session.execute(contourCommand(target, std::move(spec.value())));
}

ShapeFacts shapeFacts(const domain::Part& part) {
    ShapeFacts f;
    f.shaped = domain::hasShape(part);
    f.routed = !domain::isRectangularOutline(part);
    if (!part.shape) {
        f.outlineArea = part.length * part.width;
        f.segments = 4;
        return f;
    }
    const auto& shape = *part.shape;
    f.outlineArea = std::abs(geometry::signedArea(shape.outer));
    for (const auto& hole : shape.cutouts) f.outlineArea -= std::abs(geometry::signedArea(hole));
    f.segments = shape.outer.vertices.size();
    f.cutouts = shape.cutouts.size();
    for (const auto& band : shape.outerEdges)
        if (band) ++f.bandedSegments;
    return f;
}

QString shapeSummary(const ShapeFacts& facts) {
    if (!facts.shaped) return ContourText::tr("rectangle");
    if (facts.routed) return ContourText::tr("shaped part (routed)");
    return ContourText::tr("shaped part (rectangular outline, sawn)");
}

} // namespace fos::app
