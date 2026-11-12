// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/units.hpp"
#include "fos/nesting/nest.hpp"

#include <cmath>
#include <set>
#include <string>

namespace fos::nesting {

namespace {

using geometry::Polygon2;

std::string fmt(double v) { return core::formatLength(v, 3); }

void err(core::Diagnostics& out, std::string code, std::string message, std::string entity) {
    out.push_back({core::Severity::Error, std::move(code), std::move(message), std::move(entity)});
}

bool finitePoly(const Polygon2& p) {
    for (auto v : p.vertices)
        if (!std::isfinite(v.x) || !std::isfinite(v.y)) return false;
    return true;
}

double effectiveStep(const Part& part, const NestOptions& options) {
    return part.rotationStepDeg != 0 ? part.rotationStepDeg : options.rotationStepDeg;
}

} // namespace

geometry::Rect2 placementArea(const Job& job, const Sheet& sheet) noexcept {
    const auto& m = sheet.margins;
    const double e = job.edgeSpacing;
    geometry::Rect2 r{{m.left + e, m.bottom + e}, {sheet.length - m.right - e, sheet.width - m.top - e}};
    if (r.min.x > r.max.x || r.min.y > r.max.y) return geometry::Rect2{};
    return r;
}

std::vector<double> allowedRotations(const Part& part, const NestOptions& options) {
    switch (part.rotation) {
    case RotationRule::None: return {0.0};
    case RotationRule::Half: return {0.0, 180.0};
    case RotationRule::Quarter: return {0.0, 90.0, 180.0, 270.0};
    case RotationRule::Step: {
        const double step = effectiveStep(part, options);
        std::vector<double> r;
        if (!(step > 0.0) || !(step <= 360.0)) return {0.0};
        for (int k = 0; k * step < 360.0 - 1e-9 && k < 3600; ++k) r.push_back(k * step);
        return r;
    }
    }
    return {0.0};
}

core::Diagnostics validateJob(const Job& job, const NestOptions& options) {
    core::Diagnostics out;
    if (job.sheets.empty()) err(out, "nesting.job.no_sheets", "The nesting job has no stock sheets.", "");
    if (job.parts.empty()) err(out, "nesting.job.no_parts", "The nesting job has no parts.", "");
    if (!std::isfinite(job.partSpacing) || job.partSpacing < 0)
        err(out, "nesting.job.spacing", "Part spacing " + fmt(job.partSpacing) + " mm must be a finite value >= 0.", "");
    if (!std::isfinite(job.edgeSpacing) || job.edgeSpacing < 0)
        err(out, "nesting.job.spacing", "Edge spacing " + fmt(job.edgeSpacing) + " mm must be a finite value >= 0.", "");
    if (!std::isfinite(job.minRemnantLength) || job.minRemnantLength < 0)
        err(out, "nesting.job.remnant", "Minimum remnant length " + fmt(job.minRemnantLength) +
                                            " mm must be a finite value >= 0.", "");
    if (!std::isfinite(options.rotationStepDeg) || !(options.rotationStepDeg > 0) || options.rotationStepDeg > 360)
        err(out, "nesting.job.rotation_step",
            "Default rotation step " + fmt(options.rotationStepDeg) + " degrees must be in (0, 360].", "");

    std::set<std::string> ids;
    for (const Sheet& s : job.sheets) {
        if (s.id.empty()) err(out, "nesting.job.sheet_id", "A stock sheet has an empty id.", "");
        else if (!ids.insert("s:" + s.id).second)
            err(out, "nesting.job.sheet_id", "Stock sheet id '" + s.id + "' is used more than once.", s.id);
        if (!std::isfinite(s.length) || !std::isfinite(s.width) || s.length <= 0 || s.width <= 0) {
            err(out, "nesting.job.sheet_size",
                "Sheet '" + s.id + "' has size " + fmt(s.length) + " x " + fmt(s.width) + " mm; both must be positive.",
                s.id);
            continue;
        }
        if (s.quantity < 0 && s.quantity != kUnlimited)
            err(out, "nesting.job.sheet_quantity",
                "Sheet '" + s.id + "' has quantity " + std::to_string(s.quantity) + "; use >= 0 or kUnlimited.", s.id);
        const auto& m = s.margins;
        if (!(m.left >= 0 && m.right >= 0 && m.bottom >= 0 && m.top >= 0)) {
            err(out, "nesting.job.margin", "Sheet '" + s.id + "' has a negative or invalid margin.", s.id);
            continue;
        }
        const auto area = placementArea(job, s);
        if (area.empty() || area.width() <= 0 || area.height() <= 0)
            err(out, "nesting.job.no_area",
                "Sheet '" + s.id + "' (" + fmt(s.length) + " x " + fmt(s.width) +
                    " mm) has no placement area left after margins and an edge spacing of " + fmt(job.edgeSpacing) +
                    " mm.",
                s.id);
    }

    long long instances = 0;
    for (const Part& p : job.parts) {
        const std::string name = "Part '" + p.id + "'";
        if (p.id.empty()) err(out, "nesting.job.part_id", "A part has an empty id.", "");
        else if (!ids.insert("p:" + p.id).second)
            err(out, "nesting.job.part_id", "Part id '" + p.id + "' is used more than once.", p.id);
        if (p.quantity < 1)
            err(out, "nesting.job.part_quantity",
                name + " has quantity " + std::to_string(p.quantity) + "; it must be at least 1.", p.id);
        else
            instances += p.quantity;
        if (p.outline.size() > 1000)
            err(out, "nesting.job.outline", name + " has " + std::to_string(p.outline.size()) +
                                                " outline vertices; at most 1000 are supported.", p.id);
        if (!finitePoly(p.outline) || !geometry::isSimple(p.outline) ||
            geometry::winding(p.outline) == geometry::Winding::Degenerate) {
            err(out, "nesting.job.outline",
                name + " outline (" + std::to_string(p.outline.size()) +
                    " vertices) is not a simple polygon with positive area.",
                p.id);
            continue;
        }
        for (std::size_t h = 0; h < p.holes.size(); ++h) {
            const auto& hole = p.holes[h];
            if (!finitePoly(hole) || !geometry::isSimple(hole) ||
                geometry::winding(hole) == geometry::Winding::Degenerate)
                err(out, "nesting.job.hole", name + " hole " + std::to_string(h) + " is not a simple polygon.", p.id);
            else if (!geometry::polygonContainsPolygon(p.outline, hole))
                err(out, "nesting.job.hole", name + " hole " + std::to_string(h) + " is not inside the outline.",
                    p.id);
        }
        if (p.rotation == RotationRule::Step && p.rotationStepDeg != 0 &&
            (!std::isfinite(p.rotationStepDeg) || p.rotationStepDeg < 0 || p.rotationStepDeg > 360))
            err(out, "nesting.job.rotation_step",
                name + " has rotation step " + fmt(p.rotationStepDeg) + " degrees; it must be in (0, 360].", p.id);
    }
    if (instances > 10000)
        err(out, "nesting.job.too_many",
            "The job has " + std::to_string(instances) + " part instances; at most 10000 are supported.", "");
    return out;
}

} // namespace fos::nesting
