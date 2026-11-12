// SPDX-License-Identifier: AGPL-3.0-only
/// Back panel (Overlay / Grooved / Inset) and the grooves that carry a grooved back.
#include "gen_context.hpp"

namespace fos::furniture::detail {
namespace {
/// Through groove along the whole length (local x) of part `i`, centred at local y = `v`.
void addGroove(Gen& g, std::size_t i, domain::Face face, double v, double width, double depth) {
    domain::Part& part = g.assembly.parts[i];
    domain::Operation op;
    op.id = domain::OperationId(part.id.str() + "/op.groove.back");
    op.shape = domain::Groove{face, 0.0, v, part.length, v, width, depth};
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

    const double centre = g.v(p::kGrooveCenter), width = g.v(p::kGrooveWidth), depth = g.v(p::kGrooveDepth);
    // Inner faces: left side B, right side A, bottom A (upper face), top / rear rail B (underside).
    addGroove(g, g.index("side.left"), domain::Face::B, centre, width, depth);
    addGroove(g, g.index("side.right"), domain::Face::A, centre, width, depth);
    addGroove(g, g.index("bottom"), domain::Face::A, centre, width, depth);
    if (o.top == TopKind::Panel) addGroove(g, g.index("top"), domain::Face::B, centre, width, depth);
    else addGroove(g, g.index("rail.back"), domain::Face::B, centre - (g.v(p::kDepth) - g.v(p::kRailWidth)), width, depth);
    return {};
}

} // namespace fos::furniture::detail
