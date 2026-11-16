// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Test data: basicCabinet() plus the PHASE12 example shaped parts in an extra assembly.
#include "fos/domain/fixtures.hpp"
#include "fos/domain/model.hpp"

#include <stdexcept>

namespace fos::nesting::test {

inline domain::Part shapedPart(std::string id, std::string name, double l, double w, domain::PartShape shape,
                               domain::GrainDirection grain = domain::GrainDirection::None) {
    domain::Part p;
    p.id = domain::PartId(std::move(id));
    p.name = std::move(name);
    p.length = l;
    p.width = w;
    p.thickness = 18;
    p.material = domain::MaterialId("mat.pb18");
    p.grain = grain;
    p.shape = std::move(shape);
    return p;
}

/// 600 x 400 rounded rectangle, R50 on all corners, raw (no bands).
inline domain::Part roundedPanel() {
    auto c = geometry::roundedRectangleContour(600, 400, {50, 50, 50, 50});
    if (!c) throw std::logic_error("rounded rectangle rejected");
    return shapedPart("shaped/rounded", "Rounded panel", 600, 400, domain::PartShape{c.value(), {}, {}});
}

/// 800 x 600 side panel: rectangle up to y = 500 whose top segment (800,500) -> (0,500) is an
/// arc with bulge 0.25 (sagitta = 0.25 * 800 / 2 = 100 mm, so the top reaches y = 600).
/// Grain along the length (grained particleboard).
inline domain::Part archedSide() {
    geometry::Contour c;
    c.vertices = {{{0, 0}, 0}, {{800, 0}, 0}, {{800, 500}, 0.25}, {{0, 500}, 0}};
    return shapedPart("shaped/arched", "Arched side", 800, 600, domain::PartShape{c, {}, {}},
                      domain::GrainDirection::AlongLength);
}

/// 1200 x 600 worktop with a 500 x 400 sink cut-out at (350, 100) and a 35 mm tap hole at (1000, 300).
inline domain::Part worktop() {
    geometry::Contour sink = geometry::translated(geometry::rectangleContour(500, 400), {350, 100});
    return shapedPart("shaped/worktop", "Worktop", 1200, 600,
                      domain::PartShape{geometry::rectangleContour(1200, 600),
                                        {sink, geometry::circleContour({1000, 300}, 17.5)},
                                        {}});
}

/// basicCabinet() plus an assembly "shaped" holding roundedPanel, archedSide and worktop.
inline domain::Model phase12Model() {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly a;
    a.id = domain::AssemblyId("shaped");
    a.name = "Shaped parts";
    a.parts = {roundedPanel(), archedSide(), worktop()};
    m.assemblies.push_back(a);
    return m;
}

} // namespace fos::nesting::test
