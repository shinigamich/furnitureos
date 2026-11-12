// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cutting/from_model.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>

namespace fos::cutting {
namespace {

using core::formatLength;

std::string partName(const domain::Part& part) {
    return "'" + (part.name.empty() ? part.id.str() : part.name) + "' (" + part.id.str() + ")";
}

} // namespace

std::vector<Job> jobsFromModel(const domain::Model& model, const Settings& settings,
                               core::Diagnostics& diagnostics) {
    std::vector<Job> jobs;
    std::vector<int> jobOfMaterial(model.materials.size(), -1);
    std::vector<char> badSheet(model.materials.size(), 0);

    for (const domain::Assembly& assembly : model.assemblies) {
        for (const domain::Part& part : assembly.parts) {
            std::size_t mi = 0;
            while (mi < model.materials.size() && model.materials[mi].id != part.material) ++mi;
            if (mi == model.materials.size()) {
                diagnostics.push_back(
                    {core::Severity::Error, "cutting.adapter.material_missing",
                     "Part " + partName(part) + " references material '" + part.material.str() +
                         "' which is not in the model; it is not included in any cutting job.",
                     part.id.str()});
                continue;
            }
            const domain::Material& material = model.materials[mi];
            if (!(material.sheetLength > core::kLengthEps) || !(material.sheetWidth > core::kLengthEps) ||
                !std::isfinite(material.sheetLength) || !std::isfinite(material.sheetWidth)) {
                if (!badSheet[mi])
                    diagnostics.push_back(
                        {core::Severity::Error, "cutting.adapter.invalid_sheet",
                         "Material '" + material.name + "' (" + material.id.str() + ") has an invalid sheet size " +
                             formatLength(material.sheetLength) + " x " + formatLength(material.sheetWidth) +
                             " mm; its parts are not included in any cutting job.",
                         material.id.str()});
                badSheet[mi] = 1;
                continue;
            }
            // A band missing from the model counts as 0 mm in cutSize(): the piece is cut
            // oversize by the band thickness. Say so for every such edge (in edge order).
            for (const domain::EdgeSide side : domain::kAllEdges) {
                const auto& band = part.edge(side);
                if (band && !model.findEdgeBand(*band))
                    diagnostics.push_back(
                        {core::Severity::Warning, "cutting.adapter.edgeband_missing",
                         "Part " + partName(part) + " references edge band '" + band->str() + "' on edge " +
                             domain::toString(side) +
                             " which is not in the model; it is counted as 0 mm thick, so the piece is cut to "
                             "the finished size on that edge (oversize by the real band thickness).",
                         part.id.str()});
            }
            const domain::CutSize cut = domain::cutSize(part, model);
            if (!(cut.length > core::kLengthEps) || !(cut.width > core::kLengthEps) || !std::isfinite(cut.length) ||
                !std::isfinite(cut.width)) {
                diagnostics.push_back(
                    {core::Severity::Error, "cutting.adapter.invalid_size",
                     "Part " + partName(part) + " has a cut size of " + formatLength(cut.length) + " x " +
                         formatLength(cut.width) + " mm; both sides must be > 0. It is not included in any cutting job.",
                     part.id.str()});
                continue;
            }
            if (!core::approxEqual(part.thickness, material.thickness))
                diagnostics.push_back(
                    {core::Severity::Warning, "cutting.adapter.thickness_mismatch",
                     "Part " + partName(part) + " is " + formatLength(part.thickness) + " mm thick but its material '" +
                         material.name + "' is " + formatLength(material.thickness) + " mm.",
                     part.id.str()});

            if (jobOfMaterial[mi] < 0) {
                Job job;
                job.id = material.id.str();
                job.name = material.name;
                job.thickness = material.thickness;
                job.kerf = settings.kerf;
                job.trim = settings.trim;
                job.minRemnantLength = settings.minRemnantLength;
                job.minRemnantWidth = settings.minRemnantWidth;
                job.maxStages = settings.maxStages;
                job.firstCut = settings.firstCut;
                job.objective = settings.objective;
                StockSheet stock;
                stock.id = material.id.str() + "/sheet";
                stock.label = material.name;
                stock.length = material.sheetLength;
                stock.width = material.sheetWidth;
                stock.quantity = kUnlimited;
                stock.cost = material.costPerSheet;
                job.stock.push_back(stock);
                jobOfMaterial[mi] = static_cast<int>(jobs.size());
                jobs.push_back(std::move(job));
            }
            Job& job = jobs[static_cast<std::size_t>(jobOfMaterial[mi])];

            // THE grain rule lives in domain (shared with production's cut list). The material
            // grain runs along the sheet length, so a part whose width must run along the sheet
            // length is entered with its sizes swapped.
            const domain::SheetAlignment alignment = domain::sheetAlignment(part, &material);
            const bool canRotate = alignment == domain::SheetAlignment::Free;
            double length = cut.length, width = cut.width;
            if (alignment == domain::SheetAlignment::LengthAcrossSheet) std::swap(length, width);

            const auto same = std::find_if(job.pieces.begin(), job.pieces.end(), [&](const Piece& p) {
                return p.canRotate == canRotate && core::approxEqual(p.length, length) &&
                       core::approxEqual(p.width, width);
            });
            if (same != job.pieces.end()) {
                ++same->quantity;
                same->sourceIds.push_back(part.id.str());
                const std::string name = part.name.empty() ? part.id.str() : part.name;
                // Append the name unless it is already one of the " / "-separated names.
                const std::string haystack = " / " + same->label + " / ";
                if (haystack.find(" / " + name + " / ") == std::string::npos) same->label += " / " + name;
            } else {
                Piece piece;
                piece.id = part.id.str();
                piece.label = part.name.empty() ? part.id.str() : part.name;
                piece.length = length;
                piece.width = width;
                piece.quantity = 1;
                piece.canRotate = canRotate;
                piece.sourceIds.push_back(part.id.str());
                job.pieces.push_back(std::move(piece));
            }
        }
    }

    // Jobs in material order (they were created in order of first use).
    std::vector<Job> ordered;
    for (std::size_t mi = 0; mi < model.materials.size(); ++mi)
        if (jobOfMaterial[mi] >= 0) ordered.push_back(std::move(jobs[static_cast<std::size_t>(jobOfMaterial[mi])]));
    return ordered;
}

} // namespace fos::cutting
