// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/drawing_edits.hpp"

#include "fos/core/units.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace fos::app {

namespace {

class DrawingEditText {
    Q_DECLARE_TR_FUNCTIONS(DrawingEditText)
};

using geometry::Vec2;

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double len2 = geometry::dot(ab, ab);
    double t = len2 > 0 ? geometry::dot(p - a, ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return geometry::length(p - (a + ab * t));
}

double distanceToRect(Vec2 p, const drawings::Rect2& r) {
    if (r.empty()) return std::numeric_limits<double>::infinity();
    const double dx = std::max({r.min.x - p.x, 0.0, p.x - r.max.x});
    const double dy = std::max({r.min.y - p.y, 0.0, p.y - r.max.y});
    return std::hypot(dx, dy);
}

double distanceTo(Vec2 p, const drawings::Primitive& primitive) {
    if (const auto* line = std::get_if<drawings::Line>(&primitive)) return distanceToSegment(p, line->a, line->b);
    if (const auto* text = std::get_if<drawings::Text>(&primitive)) return distanceToRect(p, drawings::textBox(*text));
    return distanceToRect(p, drawings::boundsOf(primitive));
}

const drawings::DrawingEdits* storedEdits(const project::Project& project, const std::string& drawingId) {
    const auto it = project.drawingEdits.find(drawingId);
    return it == project.drawingEdits.end() ? nullptr : &it->second;
}

drawings::ElementEdit storedElementEdit(const project::Project& project, const std::string& drawingId,
                                        const std::string& id) {
    if (const auto* edits = storedEdits(project, drawingId))
        if (const auto it = edits->elements.find(id); it != edits->elements.end()) return it->second;
    return {};
}

bool isEmpty(const drawings::ElementEdit& e) {
    return !e.offset && e.textShift == Vec2{} && !e.hidden && !e.textOverride && !e.measuredAtEdit;
}

project::Command elementCommand(const std::string& drawingId, const std::string& id, const drawings::ElementEdit& e) {
    return project::commands::setElementEdit(drawingId, id,
                                             isEmpty(e) ? std::nullopt : std::optional<drawings::ElementEdit>(e));
}

/// Applies `change` to the stored user dimension `id` and returns setDrawingEdits.
template <class F>
project::Command userDimensionCommand(const project::Project& project, const std::string& drawingId,
                                      const std::string& id, F change) {
    drawings::DrawingEdits edits;
    if (const auto* stored = storedEdits(project, drawingId)) edits = *stored;
    for (auto& u : edits.dimensions)
        if (u.id == id) change(u);
    return project::commands::setDrawingEdits(drawingId, edits);
}

core::Error refused(const QString& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, message.toStdString());
}

} // namespace

std::string drawingIdOf(const DrawingRequest& request) {
    return request.kind == DrawingRequest::Kind::Assembly ? project::assemblyDrawingId(request.id)
                                                          : project::partDrawingId(request.id);
}

drawings::EditModelContext editContext(const project::Resolved& resolved, const DrawingRequest& request) {
    drawings::EditModelContext context;
    if (request.kind == DrawingRequest::Kind::Assembly) {
        context.assembly = resolved.model.findAssembly(domain::AssemblyId(request.id));
    } else {
        for (const auto& a : resolved.model.assemblies)
            if (const auto* p = a.findPart(domain::PartId(request.id))) {
                context.part = p;
                break;
            }
    }
    return context;
}

std::optional<DimensionRef> findDimension(const drawings::DrawingResult& drawing, const std::string& id) {
    for (std::size_t v = 0; v < drawing.views.size(); ++v)
        for (const auto& d : drawing.views[v].annotations.dimensions)
            if (d.id == id) return DimensionRef{v, d, d.userAuthored};
    return std::nullopt;
}

std::optional<std::size_t> viewAt(const drawings::DrawingResult& drawing, std::size_t sheetIndex, Vec2 paper) {
    if (sheetIndex >= drawing.sheets.size()) return std::nullopt;
    std::optional<std::size_t> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const auto& placed : drawing.sheets[sheetIndex].views) {
        if (!placed.paperBounds.contains(paper)) continue;
        const double d = geometry::length(placed.paperBounds.center() - paper);
        if (d >= bestDistance) continue;
        for (std::size_t v = 0; v < drawing.views.size(); ++v)
            if (drawing.views[v].sheetIndex == sheetIndex && drawing.views[v].view.id == placed.viewId) {
                best = v;
                bestDistance = d;
            }
    }
    return best;
}

std::string dimensionAt(const drawings::DrawingResult& drawing, std::size_t sheetIndex, Vec2 paper, double tolerance) {
    if (sheetIndex >= drawing.sheets.size()) return {};
    std::set<std::string> ids;
    for (const auto& v : drawing.views)
        if (v.sheetIndex == sheetIndex)
            for (const auto& d : v.annotations.dimensions)
                if (!d.hidden) ids.insert(d.id);
    std::string best;
    double bestDistance = tolerance;
    for (const auto& primitive : drawing.sheets[sheetIndex].primitives) {
        const std::string& id = drawings::elementIdOf(primitive);
        if (id.empty() || ids.count(id) == 0) continue;
        const double d = distanceTo(paper, primitive);
        if (d <= bestDistance && (best.empty() || d < bestDistance)) {
            best = id;
            bestDistance = d;
        }
    }
    return best;
}

drawings::Rect2 elementBounds(const drawings::Sheet& sheet, const std::string& elementId) {
    drawings::Rect2 box;
    for (const auto& primitive : sheet.primitives)
        if (drawings::elementIdOf(primitive) == elementId) {
            if (const auto* text = std::get_if<drawings::Text>(&primitive)) box.expand(drawings::textBox(*text));
            else box.expand(drawings::boundsOf(primitive));
        }
    return box;
}

std::optional<CornerPick> nearestCorner(const drawings::GeneratedView& view, const drawings::EditModelContext& model,
                                        Vec2 viewPoint) {
    std::vector<const domain::Part*> parts;
    if (model.assembly)
        for (const auto& p : model.assembly->parts) parts.push_back(&p);
    else if (model.part)
        parts.push_back(model.part);
    std::optional<CornerPick> best;
    for (const domain::Part* part : parts) {
        for (int corner = 0; corner < 8; ++corner) {
            drawings::ModelAnchor anchor{part->id.str(), corner, {}};
            const auto r = drawings::resolveAnchor(anchor, view.view, model);
            if (!r.point) continue;
            const double d = geometry::length(*r.point - viewPoint);
            if (!best || d < best->distance) best = CornerPick{anchor, *r.point, d, part->name};
        }
    }
    return best;
}

project::Command dimensionOffsetCommand(const project::Project& project, const std::string& drawingId,
                                        const DimensionRef& dimension, double offset) {
    const std::string& id = dimension.dimension.id;
    if (dimension.user)
        return userDimensionCommand(project, drawingId, id, [offset](drawings::UserDimension& u) { u.offset = offset; });
    drawings::ElementEdit e = storedElementEdit(project, drawingId, id);
    e.offset = offset;
    return elementCommand(drawingId, id, e);
}

core::Result<project::Command> hideDimensionCommand(const project::Project& project, const std::string& drawingId,
                                                    const DimensionRef& dimension) {
    if (dimension.user)
        return refused(DrawingEditText::tr("A user dimension cannot be hidden; remove it instead."));
    drawings::ElementEdit e = storedElementEdit(project, drawingId, dimension.dimension.id);
    e.hidden = true;
    return elementCommand(drawingId, dimension.dimension.id, e);
}

project::Command dimensionTextCommand(const project::Project& project, const std::string& drawingId,
                                      const DimensionRef& dimension, const QString& text) {
    const std::string value = text.trimmed().toStdString();
    const std::string& id = dimension.dimension.id;
    if (dimension.user) {
        const double measured = drawings::measuredValue(dimension.dimension);
        return userDimensionCommand(project, drawingId, id, [&](drawings::UserDimension& u) {
            if (value.empty()) {
                u.textOverride.reset();
                u.measuredAtEdit.reset();
            } else {
                u.textOverride = value;
                u.measuredAtEdit = measured;
            }
        });
    }
    drawings::ElementEdit e = storedElementEdit(project, drawingId, id);
    if (value.empty()) {
        e.textOverride.reset();
        e.measuredAtEdit.reset();
    } else {
        drawings::setTextOverride(e, dimension.dimension, value);
    }
    return elementCommand(drawingId, id, e);
}

core::Result<project::Command> removeEditCommand(const project::Project& project, const std::string& drawingId,
                                                 const std::string& id) {
    const drawings::DrawingEdits* stored = storedEdits(project, drawingId);
    const auto notFound = [&] {
        return core::makeError(core::ErrorCode::NotFound,
                               DrawingEditText::tr("The drawing has no stored edit “%1”.")
                                   .arg(QString::fromStdString(id))
                                   .toStdString());
    };
    if (!stored) return notFound();
    if (stored->elements.count(id) != 0) return project::commands::setElementEdit(drawingId, id, std::nullopt);
    drawings::DrawingEdits edits = *stored;
    const auto drop = [&](auto& list) {
        const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& a) { return a.id == id; }), list.end());
        return list.size() != before;
    };
    const bool removed = drop(edits.dimensions) || drop(edits.texts) || drop(edits.leaders) || drop(edits.detailMarkers);
    if (!removed) return notFound();
    return project::commands::setDrawingEdits(drawingId, edits.empty() ? std::nullopt
                                                                       : std::optional<drawings::DrawingEdits>(edits));
}

core::Result<project::Command> addUserDimensionCommand(const project::Project& project, const std::string& drawingId,
                                                       const drawings::GeneratedView& view,
                                                       const drawings::EditModelContext& model,
                                                       drawings::DimensionOrientation orientation,
                                                       const drawings::ModelAnchor& first,
                                                       const drawings::ModelAnchor& second, double offset) {
    drawings::DrawingEdits edits;
    if (const auto* stored = storedEdits(project, drawingId)) edits = *stored;
    std::set<std::string> taken;
    for (const auto& u : edits.dimensions) taken.insert(u.id);
    for (const auto& u : edits.texts) taken.insert(u.id);
    for (const auto& u : edits.leaders) taken.insert(u.id);
    for (const auto& u : edits.detailMarkers) taken.insert(u.id);
    int n = 1;
    while (taken.count("user.dim." + std::to_string(n)) != 0) ++n;

    drawings::UserDimension u;
    u.id = "user.dim." + std::to_string(n);
    u.viewId = view.view.id;
    u.orientation = orientation;
    u.offset = offset;
    u.anchor1 = first;
    u.anchor2 = second;
    std::string problem;
    const auto resolved = drawings::resolveUserDimension(u, view.view, model, problem);
    if (!resolved)
        return refused(DrawingEditText::tr("The dimension cannot be attached: %1").arg(QString::fromStdString(problem)));
    if (drawings::measuredValue(*resolved) <= core::kLengthEps)
        return refused(DrawingEditText::tr("Both corners lie at the same position in the measured direction; "
                                           "pick two corners that are apart."));
    edits.dimensions.push_back(u);
    return project::commands::setDrawingEdits(drawingId, edits);
}

std::vector<DrawingEditRow> drawingEditRows(const project::Project& project, const std::string& drawingId,
                                            const drawings::DrawingResult* drawing) {
    std::vector<DrawingEditRow> rows;
    const drawings::DrawingEdits* stored = storedEdits(project, drawingId);
    if (!stored) return rows;
    using Kind = DrawingEditRow::Kind;
    for (const auto& [id, e] : stored->elements) {
        DrawingEditRow row;
        row.id = id;
        row.hidden = e.hidden;
        row.offset = e.offset;
        row.text = e.textOverride;
        rows.push_back(row);
    }
    for (const auto& u : stored->dimensions) {
        DrawingEditRow row{Kind::UserDimension, u.id, false, u.offset, u.textOverride, false, false};
        rows.push_back(row);
    }
    for (const auto& u : stored->texts) rows.push_back({Kind::UserText, u.id, false, std::nullopt, u.text, false, false});
    for (const auto& u : stored->leaders)
        rows.push_back({Kind::UserLeader, u.id, false, std::nullopt, u.text, false, false});
    for (const auto& u : stored->detailMarkers)
        rows.push_back({Kind::UserDetailMarker, u.id, false, std::nullopt, u.label, false, false});
    if (drawing) {
        const auto& report = drawing->edits;
        for (auto& row : rows) {
            row.orphan = std::binary_search(report.orphanIds.begin(), report.orphanIds.end(), row.id);
            for (const auto& d : report.diagnostics)
                if (d.code == "drawings.edits.stale_override" && d.entityId == row.id) row.stale = true;
        }
    }
    return rows;
}

QString editRowText(const DrawingEditRow& row) {
    QStringList parts;
    using Kind = DrawingEditRow::Kind;
    switch (row.kind) {
    case Kind::Element: break;
    case Kind::UserDimension: parts << DrawingEditText::tr("user dimension"); break;
    case Kind::UserText: parts << DrawingEditText::tr("user text"); break;
    case Kind::UserLeader: parts << DrawingEditText::tr("user leader"); break;
    case Kind::UserDetailMarker: parts << DrawingEditText::tr("detail marker"); break;
    }
    if (row.hidden) parts << DrawingEditText::tr("hidden");
    if (row.offset)
        parts << DrawingEditText::tr("offset %1 mm").arg(QString::fromStdString(core::formatLength(*row.offset)));
    if (row.text && (row.kind == Kind::Element || row.kind == Kind::UserDimension))
        parts << DrawingEditText::tr("text “%1”").arg(QString::fromStdString(*row.text));
    if (row.stale) parts << DrawingEditText::tr("STALE: the model value changed");
    if (row.orphan) parts << DrawingEditText::tr("ORPHAN: not in the drawing any more");
    return parts.join(QStringLiteral(", "));
}

core::Diagnostics editFindings(const drawings::DrawingResult& drawing) {
    core::Diagnostics out;
    for (const auto& d : drawing.edits.diagnostics)
        if (d.code.rfind("drawings.edits.", 0) == 0) out.push_back(d);
    return out;
}

} // namespace fos::app
