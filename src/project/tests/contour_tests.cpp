// SPDX-License-Identifier: AGPL-3.0-only
// Parametric part contours in the .fosp payload and through undoable commands.
#include "test_support.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/furniture/panel.hpp"
#include "fos/project/codec.hpp"

namespace fos::project::test {
namespace {

const char* kSavedAt = "2026-11-12T12:00:00Z";

furniture::ContourSpec roundedFront(const std::string& r) {
    furniture::ContourSpec s;
    s.kind = furniture::ContourKind::RoundedCorners;
    s.radii = {r, r, "", ""};
    return s;
}

/// A spec with every field set (also the ones its kind does not use) to prove the codec is lossless.
furniture::ContourSpec everyField() {
    furniture::ContourSpec s;
    s.kind = furniture::ContourKind::Custom;
    s.radii = {"1", "2", "", "contour.width / 100"};
    s.arcEdge = domain::EdgeSide::W1;
    s.arcRise = "12.5";
    s.arcDirection = furniture::ArcDirection::Concave;
    s.vertices = {{"0", "0", 0.0, domain::EdgeSide::L1},
                  {"contour.length", "0", 0.0, std::nullopt},
                  {"contour.length", "contour.width", 0.0, std::nullopt},
                  {"contour.length - 600", "contour.width", 0.0, std::nullopt},
                  {"contour.length - 600", "300", 0.0, domain::EdgeSide::L2},
                  {"0", "300", 0.0, std::nullopt}};
    s.cutouts = {{furniture::CutoutKind::Rectangle, "700", "50", "300", "200", "10"},
                 {furniture::CutoutKind::Circle, "1000", "450", "", "", "17.5"}};
    return s;
}

Project projectWithContours() {
    Project p = newProject(meta());
    Item cab = baseCabinetItem();
    EXPECT_TRUE(furniture::setPartContour(*cab.cabinet(), p.libraries, "shelf.1", roundedFront("30")).ok());
    p.items.push_back(cab);
    PanelItem panel;
    panel.id = domain::AssemblyId("top");
    panel.panel = furniture::makePanel(domain::PartId("top/panel"), "L top", domain::MaterialId(defaults::kParticleboard18));
    EXPECT_TRUE(panel.panel.parameters.setValue("length", 1200).ok());
    EXPECT_TRUE(panel.panel.parameters.setValue("width", 600).ok());
    panel.panel.edges[0] = domain::EdgeBandId(defaults::kAbs2);
    panel.panel.contour = everyField();
    p.items.push_back(Item{panel, formats::Json::object()});
    return p;
}

Document docWithCabinet() {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    return Document(p);
}

std::string state(const Document& d) { return projectToJson(d.project()).dump(); }

} // namespace

TEST(ProjectContour, SaveReloadRoundTripIsLossless) {
    const Project p = projectWithContours();
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok()) << text.error().message;
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_FALSE(core::hasErrors(loaded.value().diagnostics));
    const Project& q = loaded.value().project;
    ASSERT_EQ(q.items.size(), 2u);
    EXPECT_EQ(q.items[0].cabinet()->materials, p.items[0].cabinet()->materials);
    ASSERT_TRUE(q.items[1].panel()->panel.contour.has_value());
    EXPECT_EQ(*q.items[1].panel()->panel.contour, everyField());
    // Byte for byte on the second save.
    auto again = serializeProject(q, kSavedAt);
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(again.value(), text.value());
    // The generated parts carry the shapes (shelf: 600 - 36 - 2 = 562 long, R 30 front corners).
    const domain::Part* shelf = findPart(loaded.value().resolved, "cab1/shelf.1");
    ASSERT_NE(shelf, nullptr);
    ASSERT_TRUE(shelf->shape.has_value());
    EXPECT_EQ(shelf->shape->outer.size(), 6u);
    const domain::Part* top = findPart(loaded.value().resolved, "top/panel");
    ASSERT_NE(top, nullptr);
    ASSERT_TRUE(top->shape.has_value());
    EXPECT_EQ(top->shape->cutouts.size(), 2u);
    // L top: 1200 * 600 - 600 * 300 = 540000.
    EXPECT_NEAR(geometry::signedArea(top->shape->outer), 540000, 1e-6);
}

TEST(ProjectContour, FilesWithoutContoursAreUnchanged) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok());
    EXPECT_EQ(text.value().find("\"contour\""), std::string::npos);
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok());
    EXPECT_FALSE(loaded.value().project.items[0].cabinet()->materials.overrides.count("shelf.1"));
    EXPECT_EQ(serializeProject(loaded.value().project, kSavedAt).value(), text.value());
}

TEST(ProjectContour, StrictDecoding) {
    const formats::Json good = toJson(roundedFront("30"));
    ASSERT_TRUE(contourFromJson(good, "/c").ok());
    formats::Json unknown = good;
    unknown["colour"] = "red";
    auto u = contourFromJson(unknown, "/c");
    ASSERT_FALSE(u.ok());
    EXPECT_NE(u.error().message.find("colour"), std::string::npos) << u.error().message;
    formats::Json kind = good;
    kind["kind"] = "Oval";
    EXPECT_FALSE(contourFromJson(kind, "/c").ok());
    formats::Json radii = good;
    radii["radii"] = formats::Json::array({"1", "2"});
    EXPECT_FALSE(contourFromJson(radii, "/c").ok());
    formats::Json expr = good;
    expr["radii"] = formats::Json::array({"30 +", "", "", ""});
    auto e = contourFromJson(expr, "/c");
    ASSERT_FALSE(e.ok());
    EXPECT_NE(e.error().message.find("cannot be parsed"), std::string::npos) << e.error().message;
}

TEST(ProjectContour, SetPartContourCommandWithUndo) {
    Document doc = docWithCabinet();
    const std::string before = state(doc);
    const std::string modelBefore = modelText(resolved(doc.project()).model);
    ASSERT_TRUE(doc.execute(commands::setPartContour("cab1", "shelf.1", roundedFront("30"))).ok());
    EXPECT_EQ(doc.undoCount(), 1u);
    Resolved r = resolved(doc.project());
    const domain::Part* shelf = findPart(r, "cab1/shelf.1");
    ASSERT_NE(shelf, nullptr);
    EXPECT_TRUE(shelf->shape.has_value());
    // Invalid: 2 * 300 = 600 > 562 (shelf length) -> refused, nothing recorded.
    const std::string withContour = state(doc);
    auto bad = doc.execute(commands::setPartContour("cab1", "shelf.1", roundedFront("300")));
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("add up to 600 mm, more than the 562 mm of edge L1"), std::string::npos)
        << bad.error().message;
    EXPECT_EQ(state(doc), withContour);
    EXPECT_EQ(doc.undoCount(), 1u);
    // Clear, undo, undo.
    ASSERT_TRUE(doc.execute(commands::setPartContour("cab1", "shelf.1", std::nullopt)).ok());
    EXPECT_EQ(state(doc), before);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(state(doc), withContour);
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(state(doc), before);
    EXPECT_EQ(modelText(resolved(doc.project()).model), modelBefore);
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_EQ(state(doc), withContour);
    EXPECT_EQ(commands::setPartContour("cab1", "shelf.1", std::nullopt).label.english(),
              "Remove contour of part shelf.1 of cab1");
}

TEST(ProjectContour, SetPanelContourCommand) {
    Project p = projectWithContours();
    p.items[1].panel()->panel.contour.reset();
    Document doc(p);
    const std::string before = state(doc);
    furniture::ContourSpec arc;
    arc.kind = furniture::ContourKind::ArcEdge;
    arc.arcEdge = domain::EdgeSide::L1;
    arc.arcRise = "100";
    ASSERT_TRUE(doc.execute(commands::setPanelContour("top", arc)).ok());
    const Resolved r = resolved(doc.project());
    const domain::Part* top = findPart(r, "top/panel");
    ASSERT_NE(top, nullptr);
    ASSERT_TRUE(top->shape.has_value());
    // Rise 100 on the 1200 chord: bulge 100 / 600.
    EXPECT_DOUBLE_EQ(top->shape->outer.vertices[0].bulge, 100.0 / 600.0);
    const std::string withArc = state(doc);
    arc.arcRise = "700"; // more than half the 1200 mm edge
    EXPECT_FALSE(doc.execute(commands::setPanelContour("top", arc)).ok());
    EXPECT_EQ(state(doc), withArc);
    EXPECT_FALSE(doc.execute(commands::setPanelContour("cab1", arc)).ok()); // a cabinet
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(state(doc), before);
}

} // namespace fos::project::test

namespace fos::project::test {

// Corner cabinets (blind corner, L-shaped corner): the "corner" key survives a save / reload
// byte for byte, and files of ordinary cabinets carry no such key.
TEST(ProjectCorner, CornerCabinetsRoundTrip) {
    Project p = newProject(meta());
    Item blind, lshape;
    blind.definition = furniture::templates::blindCornerCabinet(domain::AssemblyId("bc"), defaultTemplateMaterials());
    blind.cabinet()->corner.blindSide = furniture::CornerSide::Right;
    blind.cabinet()->interior.front->hingeSide = furniture::HingeSide::Left;
    lshape.definition = furniture::templates::lCornerCabinet(domain::AssemblyId("lc"), defaultTemplateMaterials());
    p.items = {blind, lshape, baseCabinetItem()};
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok()) << text.error().message;
    EXPECT_NE(text.value().find("\"blindSide\""), std::string::npos);
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_FALSE(core::hasErrors(loaded.value().diagnostics));
    const Project& q = loaded.value().project;
    ASSERT_EQ(q.items.size(), 3u);
    EXPECT_EQ(q.items[0].cabinet()->corner, blind.cabinet()->corner);
    EXPECT_EQ(q.items[1].cabinet()->corner.kind, furniture::CornerKind::LShape);
    EXPECT_EQ(q.items[2].cabinet()->corner, furniture::CornerSpec{});
    EXPECT_EQ(serializeProject(q, kSavedAt).value(), text.value());
    EXPECT_NE(findPart(loaded.value().resolved, "bc/blind"), nullptr);
    const domain::Part* bottom = findPart(loaded.value().resolved, "lc/bottom");
    ASSERT_NE(bottom, nullptr);
    EXPECT_TRUE(bottom->shape.has_value());
    // An ordinary cabinet writes no "corner" key (older files stay byte-identical).
    Project plain = newProject(meta());
    plain.items.push_back(baseCabinetItem());
    EXPECT_EQ(serializeProject(plain, kSavedAt).value().find("\"corner\""), std::string::npos);
}

} // namespace fos::project::test

namespace fos::project::test {

// Appliance housings: the "niche" and "applianceGap" keys survive a save / reload byte for
// byte; ordinary cabinets write neither key (older files stay byte-identical).
TEST(ProjectAppliance, ApplianceHousingsRoundTrip) {
    Project p = newProject(meta());
    Item oven, dish;
    oven.definition = furniture::templates::ovenTallHousing(domain::AssemblyId("ov"), defaultTemplateMaterials());
    auto& niche = oven.cabinet()->interior.children[1].front->niche;
    niche.vent = furniture::NicheVent::BackCutout;
    niche.ventSize = 40;
    dish.definition = furniture::templates::dishwasherFront(domain::AssemblyId("dw"), defaultTemplateMaterials());
    dish.cabinet()->applianceGap->panels = furniture::GapPanels::Left;
    p.items = {oven, dish, baseCabinetItem()};
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok()) << text.error().message;
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_FALSE(core::hasErrors(loaded.value().diagnostics));
    const Project& q = loaded.value().project;
    ASSERT_EQ(q.items.size(), 3u);
    EXPECT_EQ(q.items[0].cabinet()->interior, oven.cabinet()->interior);
    EXPECT_EQ(q.items[1].cabinet()->applianceGap, dish.cabinet()->applianceGap);
    EXPECT_EQ(q.items[1].cabinet()->interior.front->kind, furniture::FrontKind::ApplianceFront);
    EXPECT_FALSE(q.items[2].cabinet()->applianceGap.has_value());
    EXPECT_EQ(serializeProject(q, kSavedAt).value(), text.value());
    const domain::Part* front = findPart(loaded.value().resolved, "dw/appliance_front");
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->custom.at(furniture::kMountingKey), furniture::kMountingApplianceDoor);
    EXPECT_NE(findPart(loaded.value().resolved, "dw/end_panel.left"), nullptr);
    Project plain = newProject(meta());
    plain.items.push_back(baseCabinetItem());
    const std::string plainText = serializeProject(plain, kSavedAt).value();
    EXPECT_EQ(plainText.find("\"applianceGap\""), std::string::npos);
    EXPECT_EQ(plainText.find("\"niche\""), std::string::npos);
}

} // namespace fos::project::test

namespace fos::project::test {

// Frame-and-panel fronts: "style" / "frame" survive a save / reload byte for byte and the
// reloaded cabinet generates the five pieces; slab fronts write neither key.
TEST(ProjectFramePanel, FramePanelFrontRoundTrips) {
    Project p = newProject(meta());
    Item item = baseCabinetItem();
    furniture::CabinetDefinition& c = *item.cabinet();
    ASSERT_TRUE(c.interior.front.has_value());
    furniture::FrontSpec& f = *c.interior.front;
    f.style = furniture::FrontStyle::FramePanel;
    f.frame.stileWidth = 65;
    f.frame.railWidth = 75;
    f.frame.panelThickness = 6;
    f.frame.grooveDepth = 9;
    f.frame.panelClearance = 1.5;
    f.frame.joint = furniture::FrameJoint::Dowel;
    f.frame.panelMaterial = c.materials.carcass;
    p.items = {item};
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok()) << text.error().message;
    EXPECT_NE(text.value().find("\"FramePanel\""), std::string::npos);
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const Project& q = loaded.value().project;
    ASSERT_EQ(q.items.size(), 1u);
    EXPECT_EQ(q.items[0].cabinet()->interior.front, c.interior.front);
    EXPECT_EQ(serializeProject(q, kSavedAt).value(), text.value());
    EXPECT_NE(findPart(loaded.value().resolved, "cab1/door.left.stile.left"), nullptr);
    EXPECT_NE(findPart(loaded.value().resolved, "cab1/door.right.panel"), nullptr);

    Project plain = newProject(meta());
    plain.items.push_back(baseCabinetItem());
    const std::string plainText = serializeProject(plain, kSavedAt).value();
    EXPECT_EQ(plainText.find("\"style\""), std::string::npos);
    EXPECT_EQ(plainText.find("\"frame\""), std::string::npos);
}

} // namespace fos::project::test
