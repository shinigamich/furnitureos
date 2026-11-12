// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/fixtures.hpp"
#include "fos/hardware/catalog.hpp"
#include "fos/hardware/placement.hpp"
#include "fos/machining/face.hpp"

#include <gtest/gtest.h>

namespace fos::hardware::testing {

/// The hand-specified reference cabinet (see fos/domain/fixtures.hpp):
///   left side   x 0..18,    y 0..560, z 0..720   (inner face = B at x = 18, local x = asm z, local y = asm y)
///   right side  x 782..800, y 0..560, z 0..720   (inner face = A at x = 782)
///   bottom      x 18..782,  y 0..560, z 0..18    (local = asm - (18,0,0))
///   top         x 18..782,  y 0..560, z 702..720
///   shelf.1     x 18..782,  y 20..560, z 351..369
struct Cabinet {
    domain::Model model = domain::fixtures::basicCabinet();
    domain::Assembly& a = model.assemblies[0];
    const domain::Part& left = *a.findPart(domain::PartId("cab1/side.left"));
    const domain::Part& right = *a.findPart(domain::PartId("cab1/side.right"));
    const domain::Part& bottom = *a.findPart(domain::PartId("cab1/bottom"));
    const domain::Part& top = *a.findPart(domain::PartId("cab1/top"));
    const domain::Part& shelf = *a.findPart(domain::PartId("cab1/shelf.1"));
    const domain::Part& back = *a.findPart(domain::PartId("cab1/back"));
};

inline domain::HardwareDefinition def(const char* id) { return builtinDefinition(id).value(); }

inline domain::Part makePart(const char* id, const char* name, double l, double w, double t,
                             geometry::Transform placement) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = name;
    p.length = l;
    p.width = w;
    p.thickness = t;
    p.placement = placement;
    return p;
}

inline const domain::Hole& hole(const domain::Operation& op) { return std::get<domain::Hole>(op.shape); }

inline void expectHole(const domain::Operation& op, const domain::Hole& expected) {
    const auto& h = hole(op);
    EXPECT_EQ(h.face, expected.face) << op.id.str();
    EXPECT_NEAR(h.u, expected.u, 1e-9) << op.id.str();
    EXPECT_NEAR(h.v, expected.v, 1e-9) << op.id.str();
    EXPECT_NEAR(h.diameter, expected.diameter, 1e-9) << op.id.str();
    EXPECT_NEAR(h.depth, expected.depth, 1e-9) << op.id.str();
    EXPECT_EQ(h.through, expected.through) << op.id.str();
}

inline void expectVec(geometry::Vec3 a, geometry::Vec3 b) {
    EXPECT_TRUE(approxEqual(a, b, 1e-9)) << "(" << a.x << ", " << a.y << ", " << a.z << ") vs (" << b.x << ", " << b.y
                                         << ", " << b.z << ")";
}

/// Two holes share one axis line in the assembly frame (directions may be opposite).
inline bool coaxial(const domain::Part& pa, const domain::Hole& ha, const domain::Part& pb, const domain::Hole& hb) {
    const auto a = machining::holeAxisInAssembly(pa, ha);
    const auto b = machining::holeAxisInAssembly(pb, hb);
    const bool parallel = std::fabs(std::fabs(dot(a.direction, b.direction)) - 1) < 1e-9;
    const bool onLine = length(cross(b.start - a.start, a.direction)) < 1e-9;
    return parallel && onLine;
}

} // namespace fos::hardware::testing
