// SPDX-License-Identifier: AGPL-3.0-only
/// Back panel (Overlay / Grooved / Inset) and the grooves that carry a grooved back.
#include "gen_context.hpp"

#include <algorithm>

namespace fos::furniture::detail {
namespace {
/// Groove along local x of part `i` from u = `u0` to `u1` (clamped to the part), centred at
/// local y = `v`.
void addGroove(Gen& g, std::size_t i, domain::Face face, double u0, double u1, double v, double width, double depth) {
    domain::Part& part = g.assembly.parts[i];
    u0 = std::clamp(u0, 0.0, part.length);
    u1 = std::clamp(u1, 0.0, part.length);
    domain::Operation op;
    op.id = domain::OperationId(part.id.str() + "/op.groove.back");
    op.shape = domain::Groove{face, u0, v, u1, v, width, depth};
    op.source = g.partId("rule.back_groove");
    op.note = "Groove for the back panel";
    part.operations.push_back(std::move(op));
}
} // namespace

core::Result<void> buildBack(Gen& g) {
    namespace p = param;
    const ConstructionOptions& o = g.def.construction;
    if (o.back == BackMount::None) return {};
    const double bt = g.v(p::kBackThickness);
    // Back: length -> +X, width -> +Z, thickness -> -Y (origin on the rear face).
    g.addPart("back", "Back panel", domain::PartRole::Back, MaterialRole::Back, g.v(p::kBackWidth), g.v(p::kBackHeight), bt,
              Transform::fromAxes(kRight, kUp, {g.v(p::kBackX), g.v(p::kBackRearY), g.v(p::kBackZ)}), Edges{});
    if (o.back != BackMount::Grooved) return {};

    // GROOVE END RULE. A groove runs through an end of its part only where that end is
    // hidden by a mating panel (the end butts against another panel's face, or another
    // panel covers it); there the groove exit cannot be seen. At an end that is exposed or
    // edge-banded the groove stops `groove_clearance` beyond the back panel, i.e. at the
    // back's extent along the part plus the same clearance the back has at the groove
    // bottoms - which is exactly where the bottoms of the crossing grooves lie. So:
    //   sides:          lower end hidden only with an OverSides bottom, upper end only with
    //                   an OverSides top; otherwise they stop at back_z - clearance and
    //                   back_z + back_height + clearance;
    //   bottom / top:   ends hidden (they butt the sides) with BetweenSides; with OverSides
    //                   they stop at back_x - clearance and back_x + back_width + clearance;
    //   rear rail:      always between the sides -> through.
    const double centre = g.v(p::kGrooveCenter), width = g.v(p::kGrooveWidth), depth = g.v(p::kGrooveDepth);
    const double clr = g.v(p::kGrooveClearance);
    const double bx0 = g.v(p::kBackX) - clr, bx1 = g.v(p::kBackX) + g.v(p::kBackWidth) + clr;
    const double bz0 = g.v(p::kBackZ) - clr, bz1 = g.v(p::kBackZ) + g.v(p::kBackHeight) + clr;
    const bool topOver = o.topJoint == JointKind::OverSides, bottomOver = o.bottomJoint == JointKind::OverSides;

    // Sides: local x = assembly z - side_bottom.
    const double sideZ = g.v(p::kSideBottom), sideL = g.v(p::kSideHeight);
    const double s0 = bottomOver ? 0.0 : bz0 - sideZ, s1 = topOver ? sideL : bz1 - sideZ;
    // Inner faces: left side B, right side A, bottom A (upper face), top / rear rail B (underside).
    addGroove(g, g.index("side.left"), domain::Face::B, s0, s1, centre, width, depth);
    addGroove(g, g.index("side.right"), domain::Face::A, s0, s1, centre, width, depth);
    // Horizontal panels: local x = assembly x - panel x.
    const auto horizontal = [&](const char* slot, domain::Face face, bool over, double x, double length) {
        addGroove(g, g.index(slot), face, over ? bx0 - x : 0.0, over ? bx1 - x : length, centre, width, depth);
    };
    horizontal("bottom", domain::Face::A, bottomOver, g.v(p::kBottomX), g.v(p::kBottomLength));
    if (o.top == TopKind::Panel) {
        horizontal("top", domain::Face::B, topOver, g.v(p::kTopX), g.v(p::kTopLength));
    } else {
        const std::size_t rail = g.index("rail.back");
        addGroove(g, rail, domain::Face::B, 0.0, g.assembly.parts[rail].length,
                  centre - (g.v(p::kDepth) - g.v(p::kRailWidth)), width, depth);
    }
    return {};
}

} // namespace fos::furniture::detail
