// SPDX-License-Identifier: AGPL-3.0-only
// Parametric part contours of cabinet slots (README "Contoured parts").
#include "fos/furniture/contour.hpp"
#include "fos/geometry/curve_intersect.hpp"
#include "test_support.hpp"

#include <cmath>

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
constexpr double kPi = 3.14159265358979323846;

ContourSpec rounded(std::string r0, std::string r1, std::string r2 = "", std::string r3 = "") {
    ContourSpec s;
    s.kind = ContourKind::RoundedCorners;
    s.radii = {std::move(r0), std::move(r1), std::move(r2), std::move(r3)};
    return s;
}

/// 800 wide default cabinet (inner width 764) with one adjustable shelf: 764 - 2 * 1 = 762
/// long, shelf_depth 540 (560 - setback 20) deep, front edge L1 banded with eb.abs2 (2 mm).
CabinetDefinition shelfCabinet() {
    CabinetDefinition d = defaultCabinet();
    EXPECT_TRUE(d.parameters.setValue("width", 800).ok());
    d.interior.shelfCount = 1;
    return d;
}

CabinetDefinition withContour(CabinetDefinition d, const std::string& slot, const ContourSpec& spec) {
    d.materials.overrides[slot].contour = spec;
    return d;
}

std::optional<domain::EdgeBandId> abs2() { return domain::EdgeBandId("eb.abs2"); }
} // namespace

TEST(Contour, ShelfWithRoundedFrontCorners) {
    const GenerationResult r = gen(withContour(shelfCabinet(), "shelf.1", rounded("30", "30")));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    const domain::Part& shelf = part(r.assembly, "shelf.1");
    expectSize(shelf, 762, 540, 18);
    ASSERT_TRUE(shelf.shape.has_value());
    const auto& c = shelf.shape->outer;
    // Segments: L1 straight (30,0)-(732,0), corner (L,0) arc, W2 straight, L2 straight,
    // W1 straight down to (0,30), corner (0,0) arc back to (30,0).
    ASSERT_EQ(c.size(), 6u);
    EXPECT_TRUE(geometry::approxEqual(c.vertices[0].point, {30, 0}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[1].point, {732, 0}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[2].point, {762, 30}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[3].point, {762, 540}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[4].point, {0, 540}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[5].point, {0, 30}));
    EXPECT_NEAR(geometry::segmentInfo(c, 1).radius, 30, 1e-9);
    EXPECT_NEAR(geometry::segmentInfo(c, 5).radius, 30, 1e-9);
    // Area: 762 * 540 = 411480 minus two corner remnants 30^2 - pi 30^2 / 4 = 900 - 706.858347
    // = 193.141653 each -> 411480 - 386.283306 = 411093.716694.
    EXPECT_NEAR(geometry::signedArea(c), 762.0 * 540.0 - 2 * (900.0 - kPi * 900.0 / 4.0), 1e-6);
    EXPECT_NEAR(geometry::signedArea(c), 411093.716694, 1e-5);
    // Bounding box invariant.
    const geometry::Rect2 b = geometry::bounds(c);
    EXPECT_NEAR(b.min.x, 0, 1e-9);
    EXPECT_NEAR(b.min.y, 0, 1e-9);
    EXPECT_NEAR(b.max.x, 762, 1e-9);
    EXPECT_NEAR(b.max.y, 540, 1e-9);
    // Front band on the front straight and both front arcs; the rest raw. Part::edges cleared.
    const std::vector<std::optional<domain::EdgeBandId>> bands{abs2(), abs2(), {}, {}, {}, abs2()};
    EXPECT_EQ(shelf.shape->outerEdges, bands);
    for (const auto& e : shelf.edges) EXPECT_FALSE(e.has_value());
    // Cut size: the band (2 mm) moves the front straight to y = 2 and the arcs to R 28 around
    // the same centres; the raw sides stay -> raw bounds 0..762 x 2..540 -> 762 x 538, the
    // same as the rectangular shelf (762 x (540 - 2)).
    const domain::Model model = toModel(libs(), {r.assembly});
    const domain::CutSize cut = domain::cutSize(shelf, model);
    EXPECT_NEAR(cut.length, 762, 1e-6);
    EXPECT_NEAR(cut.width, 538, 1e-6);
    // Other parts unaffected; box-based self-check still clean.
    EXPECT_FALSE(part(r.assembly, "side.left").shape.has_value());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

TEST(Contour, WidthChangeRegeneratesWithSameRadii) {
    CabinetDefinition d = withContour(shelfCabinet(), "shelf.1", rounded("30", "30"));
    auto edit = setParameter(d, libs(), "width", 900);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Part& shelf = part(edit.value().generated.assembly, "shelf.1");
    // 900 - 2 * 18 - 2 * 1 = 862 long.
    expectSize(shelf, 862, 540, 18);
    ASSERT_TRUE(shelf.shape.has_value());
    const auto& c = shelf.shape->outer;
    ASSERT_EQ(c.size(), 6u);
    EXPECT_TRUE(geometry::approxEqual(c.vertices[1].point, {832, 0}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[2].point, {862, 30}));
    EXPECT_NEAR(geometry::segmentInfo(c, 1).radius, 30, 1e-9);
    EXPECT_NEAR(geometry::signedArea(c), 862.0 * 540.0 - 2 * (900.0 - kPi * 900.0 / 4.0), 1e-6);
}

TEST(Contour, RadiusExpressionsFollowTheParameterGraphAndPartSize) {
    CabinetDefinition d = shelfCabinet();
    ASSERT_TRUE(d.parameters.defineConstant("corner_r", 25, parametric::ParameterKind::Length).ok());
    // Corner (L,W) uses the part's own width: 540 / 10 = 54.
    d = withContour(d, "shelf.1", rounded("corner_r", "corner_r * 2", "contour.width / 10", ""));
    const domain::Part& shelf = part(gen(d).assembly, "shelf.1");
    ASSERT_TRUE(shelf.shape.has_value());
    const auto& c = shelf.shape->outer;
    // (25,0) L1 -> (712,0) arc R50 -> (762,50) W2 -> (762,486) arc R54 -> (708,540) L2 -> (0,540) W1 -> (0,25) arc R25.
    ASSERT_EQ(c.size(), 7u);
    EXPECT_TRUE(geometry::approxEqual(c.vertices[1].point, {712, 0}));
    EXPECT_TRUE(geometry::approxEqual(c.vertices[3].point, {762, 486}));
    EXPECT_NEAR(geometry::segmentInfo(c, 3).radius, 54, 1e-9);
    // The L2 corner arc takes the L2 band (raw here); L1 corners the front band.
    const std::vector<std::optional<domain::EdgeBandId>> bands{abs2(), abs2(), {}, {}, {}, {}, abs2()};
    EXPECT_EQ(shelf.shape->outerEdges, bands);
}

TEST(Contour, RadiusTooLargeIsRejectedAndTheEditLeavesTheDefinitionUnchanged) {
    CabinetDefinition d = shelfCabinet();
    const CabinetDefinition before = d;
    auto r = setPartContour(d, libs(), "shelf.1", rounded("400", "400"));
    ASSERT_FALSE(r.ok());
    // 400 + 400 = 800 > 762.
    EXPECT_THAT(r.error().message, HasSubstr("furniture.contour.invalid"));
    EXPECT_THAT(r.error().message, HasSubstr("shelf.1"));
    EXPECT_THAT(r.error().message,
                HasSubstr("corner radii L1/W1 = 400 mm and L1/W2 = 400 mm add up to 800 mm, more than the 762 mm of edge L1"));
    EXPECT_EQ(d.materials, before.materials);
    // A negative radius and a bad expression are named with the field and the text.
    auto neg = setPartContour(d, libs(), "shelf.1", rounded("-5", ""));
    ASSERT_FALSE(neg.ok());
    EXPECT_THAT(neg.error().message, HasSubstr("corner radius L1/W1 is -5 mm"));
    auto bad = setPartContour(d, libs(), "shelf.1", rounded("no_such_param", ""));
    ASSERT_FALSE(bad.ok());
    EXPECT_THAT(bad.error().message, HasSubstr("shelf.1"));
    EXPECT_THAT(bad.error().message, HasSubstr("corner radius L1/W1 expression 'no_such_param'"));
    auto unparsable = setPartContour(d, libs(), "shelf.1", rounded("30 +", ""));
    ASSERT_FALSE(unparsable.ok());
    EXPECT_THAT(unparsable.error().message, HasSubstr("cannot be parsed"));
    EXPECT_EQ(d.materials, before.materials);
}

TEST(Contour, SetAndClearThroughTheEditApi) {
    CabinetDefinition d = shelfCabinet();
    ASSERT_TRUE(setPartContour(d, libs(), "shelf.1", rounded("30", "30")).ok());
    ASSERT_TRUE(d.materials.overrides.count("shelf.1"));
    EXPECT_TRUE(part(gen(d).assembly, "shelf.1").shape.has_value());
    auto cleared = setPartContour(d, libs(), "shelf.1", std::nullopt);
    ASSERT_TRUE(cleared.ok());
    EXPECT_FALSE(d.materials.overrides.count("shelf.1")); // nothing else was overridden
    EXPECT_EQ(dump(cleared.value().generated.assembly), dump(gen(shelfCabinet()).assembly));
    // An override with a material keeps the material when the contour is cleared.
    d.materials.overrides["shelf.1"].material = domain::MaterialId("mat.pb18");
    ASSERT_TRUE(setPartContour(d, libs(), "shelf.1", rounded("30", "30")).ok());
    ASSERT_TRUE(setPartContour(d, libs(), "shelf.1", std::nullopt).ok());
    ASSERT_TRUE(d.materials.overrides.count("shelf.1"));
    EXPECT_FALSE(d.materials.overrides.at("shelf.1").contour.has_value());
}

TEST(Contour, RectangleKindWithoutCutoutsKeepsAPlainPart) {
    const CabinetDefinition plain = shelfCabinet();
    const GenerationResult r = gen(withContour(plain, "shelf.1", ContourSpec{}));
    EXPECT_EQ(dump(r.assembly), dump(gen(plain).assembly));
    EXPECT_FALSE(part(r.assembly, "shelf.1").shape.has_value());
}

TEST(Contour, ContourOnAMissingSlotWarns) {
    const GenerationResult r = gen(withContour(shelfCabinet(), "shelf.7", rounded("30", "30")));
    EXPECT_THAT(messagesWithCode(r.diagnostics, "furniture.contour.unused"),
                ::testing::ElementsAre(HasSubstr("'shelf.7'")));
    EXPECT_EQ(dump(r.assembly), dump(gen(shelfCabinet()).assembly));
}

TEST(Contour, MachiningInARemovedCornerIsAnError) {
    // Left side: part frame x = height (up), y = depth (back); corner (0,0) is bottom-front.
    // The bottom joint (confirmats, defaults) is drilled into the side's face near that
    // corner. A R 100 corner removes everything outside the circle around (100, 100).
    const CabinetDefinition plain = defaultCabinet();
    const domain::Part& before = part(gen(plain).assembly, "side.left");
    int expected = 0;
    for (const auto& op : before.operations)
        if (const auto* h = std::get_if<domain::Hole>(&op.shape))
            if ((h->face == domain::Face::A || h->face == domain::Face::B) && h->u < 100 && h->v < 100) {
                // Outside the arc when the centre's distance from (100, 100) plus the hole
                // radius exceeds 100.
                const double dist = std::hypot(100 - h->u, 100 - h->v);
                if (dist + h->diameter / 2 > 100) ++expected;
            }
    ASSERT_GT(expected, 0) << "the default bottom joint must have a hole near the front-bottom corner";
    const GenerationResult r = gen(withContour(plain, "side.left", rounded("100", "")));
    const auto found = messagesWithCode(r.diagnostics, "furniture.contour.machining_outside");
    EXPECT_EQ(static_cast<int>(found.size()), expected);
    for (const auto& m : found) {
        EXPECT_THAT(m, HasSubstr("cab/side.left"));
        EXPECT_THAT(m, HasSubstr("leaves the contoured outline"));
    }
    for (const auto& d : r.diagnostics)
        if (d.code == "furniture.contour.machining_outside") EXPECT_EQ(d.severity, core::Severity::Error);
    // The operations are kept (reported, not silently dropped).
    EXPECT_EQ(part(r.assembly, "side.left").operations.size(), before.operations.size());
    // A small radius clear of the holes is fine.
    const GenerationResult ok = gen(withContour(plain, "side.left", rounded("5", "")));
    EXPECT_FALSE(hasCode(ok.diagnostics, "furniture.contour.machining_outside"));
}

TEST(Contour, EdgeHoleUnderAConcaveArcIsAnError) {
    // Bottom panel (between the sides): its W1 / W2 end edges carry the confirmat edge holes.
    // A concave arc on W1 (rise 30) removes material at the edge, so every edge hole on W1
    // (whose bore starts at x = 0) leaves the outline; holes on W2 stay inside.
    const CabinetDefinition plain = defaultCabinet();
    const domain::Part& bottom = part(gen(plain).assembly, "bottom");
    int onW1 = 0;
    for (const auto& op : bottom.operations)
        if (const auto* h = std::get_if<domain::Hole>(&op.shape))
            if (h->face == domain::Face::W1) ++onW1;
    ASSERT_GT(onW1, 0);
    ContourSpec arc;
    arc.kind = ContourKind::ArcEdge;
    arc.arcEdge = domain::EdgeSide::W1;
    arc.arcRise = "30";
    arc.arcDirection = ArcDirection::Concave;
    const GenerationResult r = gen(withContour(plain, "bottom", arc));
    const auto found = messagesWithCode(r.diagnostics, "furniture.contour.machining_outside");
    EXPECT_EQ(static_cast<int>(found.size()), onW1);
    for (const auto& m : found) EXPECT_THAT(m, HasSubstr("on face W1"));
}

TEST(Contour, Deterministic) {
    const CabinetDefinition d = withContour(withContour(shelfCabinet(), "shelf.1", rounded("30", "30")), "top",
                                            rounded("10", "10", "10", "10"));
    const GenerationResult a = gen(d), b = gen(d);
    ASSERT_EQ(a.assembly.parts.size(), b.assembly.parts.size());
    for (std::size_t i = 0; i < a.assembly.parts.size(); ++i) EXPECT_EQ(a.assembly.parts[i].shape, b.assembly.parts[i].shape);
    EXPECT_EQ(dump(a.assembly), dump(b.assembly));
    EXPECT_EQ(a.diagnostics.size(), b.diagnostics.size());
}

TEST(Contour, LimitsAreCheckedBeforeGeneration) {
    ContourSpec many;
    many.kind = ContourKind::Custom;
    for (std::size_t i = 0; i <= limits::kMaxContourVertices; ++i)
        many.vertices.push_back({std::to_string(i), "0", 0.0, std::nullopt});
    auto r = checkDefinitionLimits(withContour(shelfCabinet(), "shelf.1", many));
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("501 vertices; the limit is 500"));
    ContourSpec bulgy;
    bulgy.kind = ContourKind::Custom;
    bulgy.vertices = {{"0", "0", 0.0, {}}, {"10", "0", 2.0, {}}, {"10", "10", 0.0, {}}};
    auto b = checkDefinitionLimits(withContour(shelfCabinet(), "shelf.1", bulgy));
    ASSERT_FALSE(b.ok());
    EXPECT_THAT(b.error().message, HasSubstr("vertex 2 has bulge 2"));
}

TEST(Contour, RoundedShelvingTemplate) {
    const CabinetDefinition d = templates::openShelvingRounded(domain::AssemblyId("open"), templateMaterials());
    auto r = generate(d, libs());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_THAT(errorsOf(r.value().diagnostics), IsEmpty());
    EXPECT_FALSE(hasCode(r.value().diagnostics, "furniture.contour.unused"));
    EXPECT_THAT(interpenetrations(r.value().assembly), IsEmpty());
    for (const char* slot : {"shelf.1.1", "shelf.1.2", "shelf.2.1", "shelf.2.2"}) {
        const domain::Part& s = part(r.value().assembly, slot);
        ASSERT_TRUE(s.shape.has_value()) << slot;
        // 800 - 2 * 18 - 2 * 1 = 762 long; shelf_depth = 300 - 20 = 280.
        EXPECT_NEAR(geometry::signedArea(s.shape->outer), s.length * s.width - 2 * (900.0 - kPi * 900.0 / 4.0), 1e-6);
    }
    EXPECT_FALSE(part(r.value().assembly, "fixed_shelf.1").shape.has_value());
    // The radius is a parameter: 30 -> 50.
    CabinetDefinition e = d;
    auto edit = setParameter(e, libs(), templates::kShelfCornerRadius, 50);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Part& s = part(edit.value().generated.assembly, "shelf.1.1");
    EXPECT_NEAR(geometry::segmentInfo(s.shape->outer, 1).radius, 50, 1e-9);
}

} // namespace fos::furniture::test
