// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/nesting/from_model.hpp"

#include "fos/core/units.hpp"
#include "fos/geometry/contour.hpp"

#include <cmath>

namespace fos::nesting {
namespace {

using core::formatLength;

std::string partName(const domain::Part& part) {
    return "'" + (part.name.empty() ? part.id.str() : part.name) + "' (" + part.id.str() + ")";
}

/// Exact 90 degree CCW rotation about the origin: (x, y) -> (-y, x).
geometry::Polygon2 rotated90(const geometry::Polygon2& p) {
    geometry::Polygon2 out = p;
    for (auto& v : out.vertices) v = {-v.y, v.x};
    return out;
}

constexpr std::size_t kMaxVertices = 1000; // validateJob() limit per outline

} // namespace

std::vector<MaterialNestJob> nestingJobsFromModel(const domain::Model& model, const ModelSettings& settings,
                                                  core::Diagnostics& diagnostics) {
    std::vector<MaterialNestJob> jobs;
    const bool finite = std::isfinite(settings.toolDiameter) && std::isfinite(settings.clearance) &&
                        std::isfinite(settings.chordTolerance);
    if (!finite || !(settings.toolDiameter > 0) || !(settings.chordTolerance > 0) || settings.clearance < 0) {
        diagnostics.push_back({core::Severity::Error, "nesting.adapter.invalid_settings",
                               "Nesting settings are invalid: tool diameter " + formatLength(settings.toolDiameter) +
                                   " mm and chord tolerance " + formatLength(settings.chordTolerance, 3) +
                                   " mm must be > 0 and clearance " + formatLength(settings.clearance) +
                                   " mm must be >= 0. No nesting job is created.",
                               ""});
        return jobs;
    }
    std::vector<int> jobOfMaterial(model.materials.size(), -1);
    std::vector<char> badSheet(model.materials.size(), 0);

    for (const domain::Assembly& assembly : model.assemblies) {
        for (const domain::Part& part : assembly.parts) {
            if (domain::isRectangularOutline(part)) continue; // guillotine cutting
            std::size_t mi = 0;
            while (mi < model.materials.size() && model.materials[mi].id != part.material) ++mi;
            if (mi == model.materials.size()) {
                diagnostics.push_back({core::Severity::Error, "nesting.adapter.material_missing",
                                       "Shaped part " + partName(part) + " references material '" +
                                           part.material.str() +
                                           "' which is not in the model; it is not included in any nesting job.",
                                       part.id.str()});
                continue;
            }
            const domain::Material& material = model.materials[mi];
            if (!(material.sheetLength > core::kLengthEps) || !(material.sheetWidth > core::kLengthEps) ||
                !std::isfinite(material.sheetLength) || !std::isfinite(material.sheetWidth)) {
                if (!badSheet[mi])
                    diagnostics.push_back({core::Severity::Error, "nesting.adapter.invalid_sheet",
                                           "Material '" + material.name + "' (" + material.id.str() +
                                               ") has an invalid sheet size " + formatLength(material.sheetLength) +
                                               " x " + formatLength(material.sheetWidth) +
                                               " mm; its shaped parts are not included in any nesting job.",
                                           material.id.str()});
                badSheet[mi] = 1;
                continue;
            }
            auto raw = domain::cutContour(part, model);
            std::string shapeProblem;
            if (!raw) shapeProblem = "its raw outline cannot be computed: " + raw.error().message;
            else if (auto v = geometry::validateContour(raw.value()); !v)
                shapeProblem = "its raw outline is invalid: " + v.error().message;
            for (std::size_t k = 0; shapeProblem.empty() && k < part.shape->cutouts.size(); ++k)
                if (auto v = geometry::validateContour(part.shape->cutouts[k]); !v)
                    shapeProblem = "cut-out " + std::to_string(k) + " is invalid: " + v.error().message;
            if (!shapeProblem.empty()) {
                diagnostics.push_back({core::Severity::Error, "nesting.adapter.invalid_shape",
                                       "Shaped part " + partName(part) + " cannot be nested: " + shapeProblem +
                                           " It is not included in any nesting job.",
                                       part.id.str()});
                continue;
            }
            // Explicit chord tolerance (settings.chordTolerance): every arc point of the polygon lies
            // on the exact arc; the polygon deviates inwards from convex arcs by at most the tolerance.
            Part np;
            np.id = part.id.str();
            np.label = part.name.empty() ? part.id.str() : part.name;
            np.outline = geometry::tessellate(raw.value(), settings.chordTolerance);
            NestedPartFrame frame;
            frame.partId = part.id.str();
            frame.exactArea = std::fabs(geometry::signedArea(raw.value()));
            frame.exactNetArea = frame.exactArea;
            std::size_t tooMany = np.outline.vertices.size() > kMaxVertices ? np.outline.vertices.size() : 0;
            for (const auto& cut : part.shape->cutouts) {
                np.holes.push_back(geometry::tessellate(cut, settings.chordTolerance));
                frame.exactNetArea -= std::fabs(geometry::signedArea(cut));
                if (np.holes.back().vertices.size() > kMaxVertices)
                    tooMany = std::max(tooMany, np.holes.back().vertices.size());
            }
            if (tooMany > 0) {
                diagnostics.push_back({core::Severity::Error, "nesting.adapter.too_many_vertices",
                                       "Shaped part " + partName(part) + " needs " + std::to_string(tooMany) +
                                           " polygon vertices at chord tolerance " +
                                           formatLength(settings.chordTolerance, 3) +
                                           " mm, more than the nesting limit of 1000; use a larger chord tolerance. "
                                           "It is not included in any nesting job.",
                                       part.id.str()});
                continue;
            }
            if (!core::approxEqual(part.thickness, material.thickness))
                diagnostics.push_back({core::Severity::Warning, "nesting.adapter.thickness_mismatch",
                                       "Shaped part " + partName(part) + " is " + formatLength(part.thickness) +
                                           " mm thick but its material '" + material.name + "' is " +
                                           formatLength(material.thickness) + " mm.",
                                       part.id.str()});
            // THE grain rule (domain::sheetAlignment); the material grain runs along the sheet length.
            switch (domain::sheetAlignment(part, &material)) {
            case domain::SheetAlignment::Free: np.rotation = RotationRule::Quarter; break;
            case domain::SheetAlignment::LengthAlongSheet: np.rotation = RotationRule::Half; break;
            case domain::SheetAlignment::LengthAcrossSheet:
                np.rotation = RotationRule::Half;
                np.outline = rotated90(np.outline);
                for (auto& h : np.holes) h = rotated90(h);
                frame.preRotationDeg = 90;
                break;
            }
            np.mirrorAllowed = false;

            if (jobOfMaterial[mi] < 0) {
                MaterialNestJob mj;
                mj.materialId = material.id.str();
                mj.materialName = material.name;
                mj.thickness = material.thickness;
                mj.job.partSpacing = settings.toolDiameter + settings.clearance + 2 * settings.chordTolerance;
                mj.job.edgeSpacing = settings.toolDiameter + settings.chordTolerance;
                mj.job.minRemnantLength = settings.minRemnantLength;
                Sheet sheet;
                sheet.id = material.id.str() + "/sheet";
                sheet.label = material.name;
                sheet.length = material.sheetLength;
                sheet.width = material.sheetWidth;
                sheet.quantity = kUnlimited;
                sheet.margins = settings.margins;
                mj.job.sheets.push_back(sheet);
                jobOfMaterial[mi] = static_cast<int>(jobs.size());
                jobs.push_back(std::move(mj));
            }
            MaterialNestJob& mj = jobs[static_cast<std::size_t>(jobOfMaterial[mi])];
            mj.job.parts.push_back(std::move(np));
            mj.frames.push_back(std::move(frame));
        }
    }
    std::vector<MaterialNestJob> ordered;
    for (std::size_t mi = 0; mi < model.materials.size(); ++mi)
        if (jobOfMaterial[mi] >= 0) ordered.push_back(std::move(jobs[static_cast<std::size_t>(jobOfMaterial[mi])]));
    return ordered;
}

} // namespace fos::nesting
