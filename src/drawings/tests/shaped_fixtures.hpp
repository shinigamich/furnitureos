// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Contoured test parts of phase 12 (ADR-0008), built directly as domain::Part with a PartShape.
#include "fos/geometry/contour.hpp"
#include "test_util.hpp"

namespace fos::drawings::testing {

inline domain::Part shapedPart(const std::string& id, const std::string& name, double L, double W, double T,
                               domain::PartShape shape) {
    domain::Part p;
    p.id = domain::PartId("cab1/" + id);
    p.name = name;
    p.length = L;
    p.width = W;
    p.thickness = T;
    p.material = domain::MaterialId("mat.pb18");
    p.shape = std::move(shape);
    return p;
}

/// 600 x 400 x 18 shelf with R 50 corners, all 8 outline segments banded with eb.abs2 (2 mm).
/// roundedRectangleContour segments: 0 bottom line (50,0)-(550,0), 1 arc c(550,50), 2 right line,
/// 3 arc c(550,350), 4 top line, 5 arc c(50,350), 6 left line, 7 arc c(50,50).
inline domain::Part roundedShelf() {
    domain::PartShape s;
    s.outer = geometry::roundedRectangleContour(600, 400, {50, 50, 50, 50}).value();
    s.outerEdges.assign(s.outer.size(), domain::EdgeBandId("eb.abs2"));
    return shapedPart("shelf.round", "Round shelf", 600, 400, 18, std::move(s));
}

/// 1200 x 600 x 38 worktop with a 500 x 400 sink cut-out at (350, 100) and a 35 mm tap hole
/// centred at (600, 540). No bands.
inline domain::Part worktop() {
    domain::PartShape s;
    s.outer = geometry::rectangleContour(1200, 600);
    s.cutouts.push_back(geometry::translated(geometry::rectangleContour(500, 400), {350, 100}));
    s.cutouts.push_back(geometry::circleContour({600, 540}, 17.5));
    return shapedPart("worktop", "Worktop", 1200, 600, 38, std::move(s));
}

/// 800 x 700 x 18 side panel whose top segment (800,600)->(0,600) has bulge 0.25:
/// chord c = 800, sagitta s = b c / 2 = 100 (top at y = 700), radius = c (1 + b^2) / (4 b) = 850,
/// centre = chord midpoint (400, 600) + left normal (0, -1) x c (1 - b^2) / (4 b) = 750 -> (400, -150).
inline domain::Part arcTopSide() {
    domain::PartShape s;
    s.outer.vertices = {{{0, 0}, 0}, {{800, 0}, 0}, {{800, 600}, 0.25}, {{0, 600}, 0}};
    return shapedPart("side.arc", "Arc-top side", 800, 700, 18, std::move(s));
}

/// basicCabinet() with the three contoured parts appended to its assembly (placements are
/// irrelevant for part drawings).
inline domain::Model modelWithShapedParts() {
    domain::Model m = domain::fixtures::basicCabinet();
    m.assemblies[0].parts.push_back(roundedShelf());
    m.assemblies[0].parts.push_back(worktop());
    m.assemblies[0].parts.push_back(arcTopSide());
    return m;
}

inline double sweepOf(const ViewArc& a) {
    double s = std::fmod(a.endDeg - a.startDeg, 360.0);
    return s <= 0 ? s + 360.0 : s;
}

inline const ViewArc* findArc(const View& v, Vec2 center, double eps = 1e-9) {
    for (const ViewArc& a : v.arcs)
        if (samePoint(a.center, center, eps)) return &a;
    return nullptr;
}

} // namespace fos::drawings::testing
