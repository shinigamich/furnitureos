// SPDX-License-Identifier: AGPL-3.0-only
// Isometric and exploded isometric assembly views (pictorial.hpp).
#include "fos/drawings/dxf.hpp"
#include "fos/drawings/pictorial.hpp"
#include "fos/drawings/svg.hpp"
#include "shaped_fixtures.hpp"
#include "test_util.hpp"

#include <map>

namespace fos::drawings {
namespace {

using testing::near;
const double kC30 = std::sqrt(3.0) / 2.0;

domain::Part box(const std::string& id, domain::PartRole role, double l, double w, double t, Vec3 at) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = id;
    p.role = role;
    p.length = l;
    p.width = w;
    p.thickness = t;
    p.placement = geometry::Transform::translation(at);
    return p;
}

domain::Assembly assemblyOf(std::vector<domain::Part> parts) {
    domain::Assembly a;
    a.id = domain::AssemblyId("asm");
    a.name = "Test";
    a.parts = std::move(parts);
    return a;
}

const PictorialPart* findPart(const PictorialView& v, const std::string& id) {
    for (const auto& p : v.parts)
        if (p.partId.str() == id) return &p;
    return nullptr;
}

std::vector<const ViewSegment*> segmentsOf(const PictorialView& v, const std::string& id) {
    std::vector<const ViewSegment*> out;
    for (const auto& s : v.segments)
        if (s.sourceId == id) out.push_back(&s);
    return out;
}

bool strictlyInsideConvex(Vec2 p, const std::vector<Vec2>& poly, double eps = 1e-3) {
    for (std::size_t k = 0; k < poly.size(); ++k)
        if (cross(poly[(k + 1) % poly.size()] - poly[k], p - poly[k]) <= eps) return false;
    return true;
}

TEST(PictorialProjection, AxesAreTrueLengthAt120Degrees) {
    // FrontLeft: X -> (cos30, sin30), Y -> (-cos30, sin30), Z -> (0, 1); each of length 1.
    const Vec2 x = isoProject(IsoDirection::FrontLeft, {1, 0, 0});
    const Vec2 y = isoProject(IsoDirection::FrontLeft, {0, 1, 0});
    const Vec2 z = isoProject(IsoDirection::FrontLeft, {0, 0, 1});
    EXPECT_TRUE(testing::samePoint(x, {kC30, 0.5}));
    EXPECT_TRUE(testing::samePoint(y, {-kC30, 0.5}));
    EXPECT_TRUE(testing::samePoint(z, {0, 1}));
    // FrontRight: X -> (cos30, -sin30), Y -> (cos30, sin30).
    EXPECT_TRUE(testing::samePoint(isoProject(IsoDirection::FrontRight, {1, 0, 0}), {kC30, -0.5}));
    EXPECT_TRUE(testing::samePoint(isoProject(IsoDirection::FrontRight, {0, 1, 0}), {kC30, 0.5}));
}

TEST(PictorialView, CabinetBoundsHandComputed) {
    const domain::Model m = domain::fixtures::basicCabinet();
    auto r = generatePictorialView(testing::cabinet(m));
    ASSERT_TRUE(r) << r.error().message;
    const PictorialView& v = r.value();
    EXPECT_EQ(v.id, "cab1/view.iso");
    // u = (x - y) cos30: min at the back panel corner x = 2, y = 563 -> -561 cos30;
    //                    max at the right side x = 800, y = 0 -> 800 cos30.
    // v = (x + y) / 2 + z: min 0 at (0,0,0); max at right side (800, 560, 720) -> 680 + 720 = 1400.
    EXPECT_NEAR(v.bounds.min.x, -561 * kC30, 1e-9);
    EXPECT_NEAR(v.bounds.max.x, 800 * kC30, 1e-9);
    EXPECT_NEAR(v.bounds.min.y, 0, 1e-9);
    EXPECT_NEAR(v.bounds.max.y, 1400, 1e-9);
    EXPECT_EQ(v.parts.size(), 6u);
    for (const auto& p : v.parts) EXPECT_TRUE(near(length(p.offset), 0)) << p.partId.str();
    for (const auto& s : v.segments) EXPECT_EQ(s.style, LineStyle::Visible);
    EXPECT_TRUE(v.diagnostics.empty());
}

TEST(PictorialView, SingleBoxShowsNineEdgesAndHidesFarCorner) {
    auto r = generatePictorialView(assemblyOf({box("b", domain::PartRole::Generic, 100, 50, 20, {0, 0, 0})}));
    ASSERT_TRUE(r);
    const PictorialView& v = r.value();
    // Three faces seen (front, left, top): 9 edges; the corner (100, 50, 0) farthest from a
    // front-left-above viewer is not an end point of any edge.
    EXPECT_EQ(v.segments.size(), 9u);
    const Vec2 hidden = isoProject(IsoDirection::FrontLeft, {100, 50, 0});
    for (const auto& s : v.segments) {
        EXPECT_FALSE(testing::samePoint(s.a, hidden, 1e-6));
        EXPECT_FALSE(testing::samePoint(s.b, hidden, 1e-6));
    }
    // Total visible length: 3 edges of 100 + 3 of 50 + 3 of 20 (true lengths on the axes) = 510.
    double total = 0;
    for (const auto& s : v.segments) total += length(s.b - s.a);
    EXPECT_NEAR(total, 510, 1e-9);
    // The silhouette is a hexagon.
    EXPECT_EQ(v.parts.front().silhouette.size(), 6u);
}

TEST(PictorialView, NearerPartClipsEdgesBehindIt) {
    // "front" box lies entirely in front (smaller y) of "rear": rear edges must not run
    // through the inside of front's silhouette; front stays complete (9 edges, 510 mm).
    auto r = generatePictorialView(assemblyOf({box("rear", domain::PartRole::Generic, 300, 50, 300, {0, 100, 0}),
                                               box("front", domain::PartRole::Generic, 100, 50, 20, {-100, 0, 100})}));
    ASSERT_TRUE(r);
    const PictorialView& v = r.value();
    const auto front = segmentsOf(v, "front");
    double frontTotal = 0;
    for (const auto* s : front) frontTotal += length(s->b - s->a);
    EXPECT_NEAR(frontTotal, 510, 1e-9);
    const auto& hull = findPart(v, "front")->silhouette;
    double rearTotal = 0;
    for (const auto* s : segmentsOf(v, "rear")) {
        rearTotal += length(s->b - s->a);
        for (double t : {0.25, 0.5, 0.75})
            EXPECT_FALSE(strictlyInsideConvex(s->a + (s->b - s->a) * t, hull)) << "rear edge inside front silhouette";
    }
    // Unoccluded rear: 3 x 300 + 3 x 50 + 3 x 300 = 1950 mm; part of it is hidden.
    EXPECT_LT(rearTotal, 1950 - 1);
    EXPECT_GT(rearTotal, 1000);
}

TEST(PictorialView, ExplodedOffsetsPerRole) {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly& a = m.assemblies.front();
    // A door in front of the carcass: x [0, 800], y [-20, -2], z [0, 720].
    domain::Part door = box("door", domain::PartRole::Door, 800, 720, 18, {0, 0, 0});
    door.placement = geometry::Transform::fromAxes({1, 0, 0}, {0, 0, 1}, {0, -2, 0}); // thickness -> -Y
    a.parts.push_back(door);
    PictorialViewOptions o;
    o.kind = PictorialKind::Exploded;
    o.explodeFactor = 1.5;
    auto r = generatePictorialView(a, o);
    ASSERT_TRUE(r) << r.error().message;
    const PictorialView& v = r.value();
    EXPECT_EQ(v.id, "cab1/view.exploded");
    // Assembly bounds now x [0, 800], y [-20, 563], z [0, 720] -> size 800 x 583 x 720.
    // Offset = (factor - 1) x size along the role axis = 0.5 x size.
    const std::map<std::string, Vec3> expected{
        {"side.left", {-400, 0, 0}}, {"side.right", {400, 0, 0}}, {"top", {0, 0, 360}},
        {"bottom", {0, 0, -360}},    {"shelf.1", {0, 0, 0}},      {"back", {0, 291.5, 0}},
        {"door", {0, -291.5, 0}}};
    for (const auto& [id, off] : expected) {
        const PictorialPart* p = findPart(v, id == "door" ? id : "cab1/" + id);
        ASSERT_NE(p, nullptr) << id;
        EXPECT_NEAR(p->offset.x, off.x, 1e-9) << id;
        EXPECT_NEAR(p->offset.y, off.y, 1e-9) << id;
        EXPECT_NEAR(p->offset.z, off.z, 1e-9) << id;
    }
    // The left side's silhouette moved by isoProject(-400, 0, 0) = (-400 cos30, -200).
    auto iso = generatePictorialView(a);
    ASSERT_TRUE(iso);
    const auto& s0 = findPart(iso.value(), "cab1/side.left")->silhouette;
    const auto& s1 = findPart(v, "cab1/side.left")->silhouette;
    ASSERT_EQ(s0.size(), s1.size());
    for (std::size_t i = 0; i < s0.size(); ++i)
        EXPECT_TRUE(testing::samePoint(s1[i] - s0[i], {-400 * kC30, -200}, 1e-9));
    // Factor 1 is the plain isometric picture.
    o.explodeFactor = 1.0;
    auto one = generatePictorialView(a, o);
    ASSERT_TRUE(one);
    EXPECT_EQ(one.value().segments.size(), iso.value().segments.size());
}

TEST(PictorialView, ExplodeDirectionRules) {
    geometry::Aabb bounds{{0, 0, 0}, {800, 560, 720}};
    domain::Part p = box("x", domain::PartRole::Side, 720, 560, 18, {391, 0, 0}); // centre x = 400
    p.placement = geometry::Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {409, 0, 0});
    EXPECT_TRUE(length(explodeDirection(p, bounds)) == 0); // centred side stays
    for (auto [role, dir] : std::vector<std::pair<domain::PartRole, Vec3>>{
             {domain::PartRole::Worktop, {0, 0, 1}},
             {domain::PartRole::Plinth, {0, 0, -1}},
             {domain::PartRole::DrawerFront, {0, -1, 0}},
             {domain::PartRole::DrawerSide, {0, -1, 0}},
             {domain::PartRole::Partition, {0, 0, 0}},
             {domain::PartRole::Generic, {0, 0, 0}}}) {
        p.role = role;
        const Vec3 d = explodeDirection(p, bounds);
        EXPECT_TRUE(near(d.x, dir.x) && near(d.y, dir.y) && near(d.z, dir.z)) << domain::toString(role);
    }
}

TEST(PictorialView, RejectsBadOptions) {
    const domain::Model m = domain::fixtures::basicCabinet();
    PictorialViewOptions o;
    o.kind = PictorialKind::Exploded;
    for (double f : {0.5, 10.5, std::nan("")}) {
        o.explodeFactor = f;
        auto r = generatePictorialView(testing::cabinet(m), o);
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    }
    o = {};
    o.chordTolerance = 0;
    EXPECT_FALSE(generatePictorialView(testing::cabinet(m), o));
    EXPECT_FALSE(generatePictorialView(assemblyOf({box("z", domain::PartRole::Generic, 0, 10, 10, {})})));
}

TEST(PictorialView, OverlapIsReported) {
    auto r = generatePictorialView(assemblyOf({box("a", domain::PartRole::Generic, 100, 100, 100, {0, 0, 0}),
                                               box("b", domain::PartRole::Generic, 100, 100, 100, {50, 50, 50})}));
    ASSERT_TRUE(r);
    ASSERT_EQ(r.value().diagnostics.size(), 1u);
    EXPECT_EQ(r.value().diagnostics[0].code, "drawings.pictorial.overlap");
    EXPECT_EQ(r.value().diagnostics[0].severity, core::Severity::Info);
}

TEST(PictorialView, ContouredPartUsesExtrudedOutline) {
    auto r = generatePictorialView(assemblyOf({testing::roundedShelf()}));
    ASSERT_TRUE(r);
    const PictorialView& v = r.value();
    // Bounds from the bounding box 600 x 400 x 18: u in [-400 cos30, 600 cos30], v in [0, 500 + 18].
    EXPECT_NEAR(v.bounds.min.x, -400 * kC30, 1e-9);
    EXPECT_NEAR(v.bounds.max.x, 600 * kC30, 1e-9);
    EXPECT_NEAR(v.bounds.max.y, 518, 1e-9);
    // Tessellated arcs: many more pieces than a box; the near-face corners of the rounded
    // outline are not sharp, so the box corner (0, 0, 18) is not drawn.
    EXPECT_GT(v.segments.size(), 30u);
    const Vec2 sharp = isoProject(IsoDirection::FrontLeft, {0, 0, 18});
    for (const auto& s : v.segments) EXPECT_FALSE(testing::samePoint(s.a, sharp, 1e-6));
}

domain::Model twoCabinets() {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly second = m.assemblies.front();
    second.id = domain::AssemblyId("cab2");
    second.name = "Second cabinet";
    for (auto& p : second.parts) p.id = domain::PartId("cab2/" + p.id.str());
    m.assemblies.push_back(second);
    return m;
}

TEST(PictorialDrawing, BalloonNumbersFollowThePartList) {
    const domain::Model m = twoCabinets();
    // Part-list numbering runs over the whole model: cab1 parts 1..6, cab2 parts 7..12.
    EXPECT_EQ(firstPartListNumber(m, domain::AssemblyId("cab1")), 1);
    EXPECT_EQ(firstPartListNumber(m, domain::AssemblyId("cab2")), 7);
    EXPECT_EQ(firstPartListNumber(m, domain::AssemblyId("nope")), 0);
    PictorialDrawingOptions o;
    o.view.kind = PictorialKind::Exploded;
    o.render.balloons = true;
    auto r = generatePictorialDrawing(m, domain::AssemblyId("cab2"), o);
    ASSERT_TRUE(r) << r.error().message;
    const auto& d = r.value();
    std::map<std::string, std::string> texts;
    std::size_t circles = 0;
    for (const Primitive& p : d.sheet.primitives) {
        const std::string& el = elementIdOf(p);
        if (el.rfind("cab2/view.exploded/balloon[", 0) != 0) continue;
        if (const auto* t = std::get_if<Text>(&p)) texts[sourceIdOf(p)] = t->text;
        if (std::holds_alternative<Circle>(p)) ++circles;
    }
    const std::vector<std::string> order{"side.left", "side.right", "bottom", "top", "shelf.1", "back"};
    ASSERT_EQ(texts.size(), 6u);
    EXPECT_EQ(circles, 6u);
    for (std::size_t i = 0; i < order.size(); ++i)
        EXPECT_EQ(texts["cab2/cab1/" + order[i]], std::to_string(7 + i)) << order[i];
    // Balloons lie outside the geometry box and do not overlap each other (2 r + gap = 9 mm).
    std::vector<Vec2> centres;
    for (const Primitive& p : d.sheet.primitives)
        if (const auto* c = std::get_if<Circle>(&p); c && !c->elementId.empty()) centres.push_back(c->center);
    for (std::size_t i = 0; i < centres.size(); ++i)
        for (std::size_t j = i + 1; j < centres.size(); ++j) EXPECT_GE(length(centres[i] - centres[j]), 9 - 1e-9);
    ASSERT_EQ(d.sheet.views.size(), 1u);
    EXPECT_EQ(d.sheet.views[0].viewId, "cab2/view.exploded");
}

TEST(PictorialDrawing, DeterministicSvgAndDxf) {
    const domain::Model m = domain::fixtures::basicCabinet();
    PictorialDrawingOptions o;
    o.view.kind = PictorialKind::Exploded;
    o.view.explodeFactor = 1.8;
    o.render.balloons = true;
    o.sheet.title.date = "2026-11-26";
    auto a = generatePictorialDrawing(m, domain::AssemblyId("cab1"), o);
    auto b = generatePictorialDrawing(m, domain::AssemblyId("cab1"), o);
    ASSERT_TRUE(a && b);
    const std::string svg = toSvg(a.value().sheet);
    EXPECT_EQ(svg, toSvg(b.value().sheet));
    EXPECT_NE(svg.find("data-element=\"cab1/view.exploded/balloon[cab1/side.left]\""), std::string::npos);
    const auto prims = pictorialModelSpacePrimitives(a.value().view, o.render, 10.0);
    const std::string dxf = toDxf(prims, DxfOptions{false, 10.0});
    EXPECT_EQ(dxf, toDxf(pictorialModelSpacePrimitives(b.value().view, o.render, 10.0), DxfOptions{false, 10.0}));
    const DxfEntityCounts counts = countDxfEntities(prims);
    EXPECT_EQ(counts.texts, 6u);
    EXPECT_GE(counts.circles, 6u); // balloon circles (+ dots written as circles)
    // Explicit scale too large for the paper fails with a clear message.
    o.sheet.scale = ScaleRequest::explicitScale(1, 1);
    auto big = generatePictorialDrawing(m, domain::AssemblyId("cab1"), o);
    ASSERT_FALSE(big);
    EXPECT_EQ(big.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_FALSE(generatePictorialDrawing(m, domain::AssemblyId("missing")));
}

} // namespace
} // namespace fos::drawings
