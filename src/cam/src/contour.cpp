// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/contour.hpp"

namespace fos::cam {

using core::ErrorCode;
using core::makeError;

bool needsContourRouting(const domain::Part& part) noexcept { return !domain::isRectangularOutline(part); }

geometry::Contour contourToMachineXY(const SetupFrame& frame, const geometry::Contour& partContour) {
    geometry::Contour out = partContour;
    for (auto& v : out.vertices) {
        const geometry::Vec3 m = frame.partToMachine({v.point.x, v.point.y, 0});
        v.point = {m.x, m.y};
        if (frame.faceUp == domain::Face::B) v.bulge = -v.bulge; // mirror image: arcs change direction
    }
    return geometry::normalizedCcw(out);
}

core::Result<std::vector<ContourTarget>> contourTargets(const domain::Part& part, const SetupFrame& frame,
                                                        const domain::Model* model) {
    std::vector<ContourTarget> out;
    if (!needsContourRouting(part)) return out;
    const domain::PartShape& shape = *part.shape;
    const domain::Model empty;
    if (!model) {
        for (const auto& band : shape.outerEdges)
            if (band)
                return makeError(ErrorCode::InvalidArgument,
                                 "Part '" + part.name + "' has edge-banded outline segments; its raw outline needs the "
                                 "model's edge bands. Pass the model (CamOptions::model / SimulationOptions::model).",
                                 part.id.str());
        model = &empty;
    }
    auto raw = domain::cutContour(part, *model);
    if (!raw)
        return makeError(raw.error().code, "The raw outline of '" + part.name + "' cannot be computed: " +
                                               raw.error().message, part.id.str());
    if (auto v = geometry::validateContour(raw.value()); !v)
        return makeError(ErrorCode::InvalidArgument,
                         "The raw outline of '" + part.name + "' is invalid: " + v.error().message, part.id.str());
    for (std::size_t k = 0; k < shape.cutouts.size(); ++k) {
        if (auto v = geometry::validateContour(shape.cutouts[k]); !v)
            return makeError(ErrorCode::InvalidArgument, "Cut-out " + std::to_string(k) + " of '" + part.name +
                                                             "' is invalid: " + v.error().message, part.id.str());
        ContourTarget t;
        t.id = domain::OperationId("contour.cutout." + std::to_string(k));
        t.cutoutIndex = k;
        t.contour = contourToMachineXY(frame, shape.cutouts[k]);
        out.push_back(std::move(t));
    }
    ContourTarget o;
    o.id = domain::OperationId("contour.outline");
    o.outer = true;
    o.contour = contourToMachineXY(frame, raw.value());
    out.push_back(std::move(o));
    for (const auto& t : out)
        for (const auto& op : part.operations)
            if (op.id == t.id)
                return makeError(ErrorCode::AlreadyExists,
                                 "Operation id '" + op.id.str() + "' of '" + part.name +
                                     "' is reserved for contour routing; rename the operation.",
                                 part.id.str());
    return out;
}

} // namespace fos::cam
