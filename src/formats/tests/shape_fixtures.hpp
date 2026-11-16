// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// The PHASE 12 example parts (ADR-0008) as test fixtures, built directly as domain::Part.
#include "fos/domain/fixtures.hpp"
#include "fos/geometry/contour.hpp"

#include <gtest/gtest.h>

namespace fos::formats::test {

inline domain::Part shapedPart(const std::string& id, double L, double W, double T, domain::PartShape shape) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = id;
    p.role = domain::PartRole::Generic;
    p.length = L;
    p.width = W;
    p.thickness = T;
    p.material = domain::MaterialId("mat.pb18");
    p.shape = std::move(shape);
    return p;
}

/// 600 x 400 x 18 panel, all four corners R 50. roundedRectangleContour gives 8 segments:
/// bottom, arc, right, arc, top, arc, left, arc. Bands: 2 mm ABS on the straights, the arcs
/// with 0.4 mm ABS except the last one (raw).
inline domain::Part roundedPanel() {
    const double radii[4] = {50, 50, 50, 50};
    domain::PartShape shape;
    shape.outer = geometry::roundedRectangleContour(600, 400, radii).value();
    const domain::EdgeBandId thick("eb.abs2"), thin("eb.abs04");
    shape.outerEdges = {thick, thin, thick, thin, thick, thin, thick, std::nullopt};
    return shapedPart("shaped/round", 600, 400, 18, std::move(shape));
}

/// Side panel 800 wide whose top segment is an arc with bulge 0.25 (rise 0.25 * 800 / 2 =
/// 100 mm) over a 600 mm high rectangle: bounding box 800 x 700.
inline domain::Part arcTopSide() {
    domain::PartShape shape;
    shape.outer.vertices = {{{0, 0}, 0}, {{800, 0}, 0}, {{800, 600}, 0.25}, {{0, 600}, 0}};
    return shapedPart("shaped/arc-top", 800, 700, 18, std::move(shape));
}

/// Worktop 1200 x 600 x 38 with a 500 x 400 sink cut-out at (350, 100) and a hob/tap hole
/// of radius 30 centred at (1000, 300).
inline domain::Part worktop() {
    domain::PartShape shape;
    shape.outer = geometry::rectangleContour(1200, 600);
    shape.cutouts = {geometry::translated(geometry::rectangleContour(500, 400), {350, 100}),
                     geometry::circleContour({1000, 300}, 30)};
    shape.outerEdges = {domain::EdgeBandId("eb.abs2"), std::nullopt, std::nullopt, std::nullopt};
    return shapedPart("shaped/worktop", 1200, 600, 38, std::move(shape));
}

/// basicCabinet plus an assembly holding the three shaped example parts.
inline domain::Model shapedModel() {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly a;
    a.id = domain::AssemblyId("asm.shaped");
    a.name = "Shaped parts";
    a.placement = geometry::Transform::translation({2000, 0, 0});
    a.parts = {roundedPanel(), arcTopSide(), worktop()};
    m.assemblies.push_back(std::move(a));
    return m;
}

} // namespace fos::formats::test
