// SPDX-License-Identifier: AGPL-3.0-only
// Shape editor view-model: editor rows <-> ContourSpec, commands, refusals, undo / redo.
#include "fos/app/cabinet_parts.hpp"
#include "fos/app/contour_rows.hpp"
#include "test_support.hpp"

#include <numbers>

namespace fos::app::test {
namespace {

using furniture::ContourKind;
using furniture::CutoutKind;
using domain::EdgeSide;

constexpr double kPi = std::numbers::pi;

TEST(ContourRows, RoundedCornersNormalisesNumbersAndKeepsExpressions) {
    ContourInput in;
    in.kind = ContourKind::RoundedCorners;
    in.radii = {QStringLiteral(" 30 "), QStringLiteral("12,5 mm"), QStringLiteral("shelf_r / 2"), QString()};
    in.arcRise = QStringLiteral("50"); // other kind: not written
    in.vertices = rectangleVertexRows();
    const auto spec = contourSpec(in);
    ASSERT_TRUE(spec.ok());
    ASSERT_TRUE(spec.value().has_value());
    const furniture::ContourSpec& s = *spec.value();
    EXPECT_EQ(s.kind, ContourKind::RoundedCorners);
    EXPECT_EQ(s.radii, (std::array<std::string, 4>{"30", "12.5", "shelf_r / 2", ""}));
    EXPECT_TRUE(s.arcRise.empty());
    EXPECT_TRUE(s.vertices.empty());
    EXPECT_TRUE(s.cutouts.empty());
}

TEST(ContourRows, RectangleWithoutCutoutsMeansNoShape) {
    ContourInput in;
    auto spec = contourSpec(in);
    ASSERT_TRUE(spec.ok());
    EXPECT_FALSE(spec.value().has_value());

    in.cutouts.push_back({CutoutKind::Circle, QStringLiteral("300"), QStringLiteral("150"), QStringLiteral("9"),
                          QStringLiteral("9"), QStringLiteral("20")});
    spec = contourSpec(in);
    ASSERT_TRUE(spec.ok());
    ASSERT_TRUE(spec.value().has_value());
    ASSERT_EQ(spec.value()->cutouts.size(), 1u);
    const auto& c = spec.value()->cutouts[0];
    EXPECT_EQ(c.kind, CutoutKind::Circle);
    EXPECT_EQ(c.x, "300");
    EXPECT_EQ(c.radius, "20");
    EXPECT_TRUE(c.length.empty()); // unused for circles
    EXPECT_TRUE(c.width.empty());
}

TEST(ContourRows, BulgeMustBeANumber) {
    ContourInput in;
    in.kind = ContourKind::Custom;
    in.vertices = rectangleVertexRows();
    in.vertices[2].bulge = QStringLiteral("0,25");
    auto spec = contourSpec(in);
    ASSERT_TRUE(spec.ok());
    EXPECT_DOUBLE_EQ(spec.value()->vertices[2].bulge, 0.25);
    EXPECT_EQ(spec.value()->vertices[1].x, "contour.length");

    in.vertices[1].bulge = QStringLiteral("round");
    spec = contourSpec(in);
    ASSERT_FALSE(spec.ok());
    EXPECT_EQ(spec.error().code, core::ErrorCode::InvalidArgument);
    const QString message = QString::fromStdString(spec.error().message);
    EXPECT_TRUE(message.contains(QStringLiteral("Vertex 2"))) << spec.error().message;
    EXPECT_TRUE(message.contains(QStringLiteral("round")));
}

TEST(ContourRows, RoundTripOfCleanSpecs) {
    furniture::ContourSpec arc;
    arc.kind = ContourKind::ArcEdge;
    arc.arcEdge = EdgeSide::W1;
    arc.arcRise = "contour.width / 8";
    arc.arcDirection = furniture::ArcDirection::Concave;
    arc.cutouts.push_back({CutoutKind::Rectangle, "350", "100", "500", "400", "10"});

    furniture::ContourSpec custom;
    custom.kind = ContourKind::Custom;
    custom.vertices = {{"0", "0", 0.0, std::nullopt},
                       {"contour.length", "0", 0.0, EdgeSide::L1},
                       {"contour.length", "contour.width", 0.25, EdgeSide::L2},
                       {"0", "contour.width", -0.5, std::nullopt}};
    custom.cutouts.push_back({CutoutKind::Circle, "100", "100", "", "", "17.5"});

    furniture::ContourSpec rounded;
    rounded.kind = ContourKind::RoundedCorners;
    rounded.radii = {"30", "30", "", ""};

    for (const auto& spec : {arc, custom, rounded}) {
        const ContourInput rows = contourInput(spec);
        const auto back = contourSpec(rows);
        ASSERT_TRUE(back.ok());
        ASSERT_TRUE(back.value().has_value());
        EXPECT_EQ(*back.value(), spec);
        EXPECT_EQ(contourInput(*back.value()), rows);
    }
    EXPECT_EQ(contourInput(std::nullopt), ContourInput{});
}

TEST(ContourRows, MoveRowClamps) {
    std::vector<int> rows{1, 2, 3, 4};
    EXPECT_EQ(moveRow(rows, 0, 1), std::optional<std::size_t>(1));
    EXPECT_EQ(rows, (std::vector<int>{2, 1, 3, 4}));
    EXPECT_EQ(moveRow(rows, 3, -10), std::optional<std::size_t>(0));
    EXPECT_EQ(rows, (std::vector<int>{4, 2, 1, 3}));
    EXPECT_EQ(moveRow(rows, 2, 5), std::optional<std::size_t>(3));
    EXPECT_EQ(rows, (std::vector<int>{4, 2, 3, 1}));
    EXPECT_FALSE(moveRow(rows, 4, 1).has_value());
}

/// Open shelving 800 x 1800 x 300: shelves are 800 - 2*18 - 2*1 = 762 long (finished, as the
/// part reports it; checked below), front corners (0,0) and (L,0) rounded R 30.
/// Area removed per corner = R^2 - pi R^2 / 4 = 900 - 225 pi.
TEST(ContourCommands, RoundedShelfUndoRedoAndRefusal) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::OpenShelving).ok());
    const std::string partId = "cab1/shelf.1.1";
    const auto target = contourTarget(s.project(), "cab1", partId);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->slot, "shelf.1.1");
    EXPECT_FALSE(contourTarget(s.project(), "cab1", "").has_value());
    const domain::Part* before = part(s, partId);
    ASSERT_NE(before, nullptr);
    EXPECT_FALSE(before->shape.has_value());
    EXPECT_DOUBLE_EQ(before->length, 762);
    const double L = before->length, W = before->width;

    ContourInput in;
    in.kind = ContourKind::RoundedCorners;
    in.radii = {QStringLiteral("30"), QStringLiteral("30"), {}, {}};
    ASSERT_TRUE(applyContourInput(s, *target, in).ok());
    const domain::Part* shaped = part(s, partId);
    ASSERT_TRUE(shaped->shape.has_value());
    const ShapeFacts facts = shapeFacts(*shaped);
    EXPECT_TRUE(facts.shaped);
    EXPECT_TRUE(facts.routed);
    EXPECT_NEAR(facts.outlineArea, L * W - 2 * (900 - 225 * kPi), 1e-6);
    EXPECT_EQ(facts.segments, 6u); // 4 sides + 2 corner arcs
    EXPECT_EQ(shapeSummary(facts), QStringLiteral("shaped part (routed)"));
    EXPECT_EQ(currentContour(s.project(), *target)->radii[0], "30");

    // Refused: a radius larger than the shelf; nothing changes.
    const auto revision = s.revision();
    const auto stored = currentContour(s.project(), *target);
    in.radii[0] = QStringLiteral("5000");
    const auto refused = applyContourInput(s, *target, in);
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("furniture.contour.invalid"), std::string::npos) << refused.error().message;
    EXPECT_EQ(s.revision(), revision);
    EXPECT_EQ(currentContour(s.project(), *target), stored);

    // "Remove override" (material / edges) keeps the shape.
    const auto& cabinet = *s.project().findItem("cab1")->cabinet();
    ASSERT_TRUE(s.execute(partMaterialCommand(cabinet, "shelf.1.1", std::string(project::defaults::kMdf18))).ok());
    ASSERT_TRUE(s.execute(clearPartOverrideCommand(*s.project().findItem("cab1")->cabinet(), "shelf.1.1")).ok());
    EXPECT_EQ(currentContour(s.project(), *target), stored);
    ASSERT_TRUE(s.undo().ok());
    ASSERT_TRUE(s.undo().ok());

    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(part(s, partId)->shape.has_value());
    EXPECT_FALSE(currentContour(s.project(), *target).has_value());
    ASSERT_TRUE(s.redo().ok());
    EXPECT_TRUE(part(s, partId)->shape.has_value());

    // Rectangle without cut-outs removes the shape.
    ASSERT_TRUE(applyContourInput(s, *target, ContourInput{}).ok());
    EXPECT_FALSE(part(s, partId)->shape.has_value());
    EXPECT_EQ(shapeSummary(shapeFacts(*part(s, partId))), QStringLiteral("rectangle"));
}

/// Free panel 600 x 300 (furniture::makePanel), circular hole R 20: area 600*300 - 400 pi.
TEST(ContourCommands, PanelCircularCutout) {
    Session s;
    s.newProject();
    project::Item item{project::PanelItem{
        domain::AssemblyId("p1"), geometry::Transform{},
        furniture::makePanel(domain::PartId("p1/panel"), "Filler", domain::MaterialId(project::defaults::kParticleboard18))}};
    ASSERT_TRUE(s.execute(project::commands::addItem(item)).ok());
    const auto target = contourTarget(s.project(), "p1", "");
    ASSERT_TRUE(target.has_value());
    EXPECT_TRUE(target->slot.empty());
    EXPECT_EQ(contourTarget(s.project(), "p1", "p1/panel"), target);

    ContourInput in;
    in.cutouts.push_back({CutoutKind::Circle, QStringLiteral("300"), QStringLiteral("150"), {}, {}, QStringLiteral("20")});
    ASSERT_TRUE(applyContourInput(s, *target, in).ok());
    const domain::Part* p = part(s, "p1/panel");
    ASSERT_NE(p, nullptr);
    const ShapeFacts facts = shapeFacts(*p);
    EXPECT_TRUE(facts.routed);
    EXPECT_EQ(facts.cutouts, 1u);
    EXPECT_NEAR(facts.outlineArea, 600.0 * 300.0 - 400.0 * kPi, 1e-6);

    // A hole leaving the panel is refused by the core.
    in.cutouts[0].x = QStringLiteral("590");
    const auto revision = s.revision();
    EXPECT_FALSE(applyContourInput(s, *target, in).ok());
    EXPECT_EQ(s.revision(), revision);

    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(part(s, "p1/panel")->shape.has_value());
}

} // namespace
} // namespace fos::app::test
