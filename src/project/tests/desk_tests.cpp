// SPDX-License-Identifier: AGPL-3.0-only
// Desks and tables (furniture::DeskSpec) in the .fosp payload and through addItem.
#include "test_support.hpp"

#include "fos/furniture/templates.hpp"
#include "fos/project/codec.hpp"
#include "fos/project/document.hpp"

namespace fos::project::test {
namespace {
const char* kSavedAt = "2026-11-12T12:00:00Z";
Item templateItem(furniture::CabinetDefinition (*make)(domain::AssemblyId, const furniture::templates::TemplateMaterials&),
                  const char* id) {
    Item item;
    item.definition = make(domain::AssemblyId(id), defaultTemplateMaterials());
    return item;
}
} // namespace

TEST(ProjectDesk, RoundTripIsLosslessAndKeysOnlyWhenUsed) {
    Document doc(newProject(meta()));
    ASSERT_TRUE(doc.execute(commands::addItem(templateItem(&furniture::templates::desk, "desk"))).ok());
    Item t = templateItem(&furniture::templates::table, "table");
    t.cabinet()->desk->frontCornerRadius = 20;
    t.cabinet()->desk->legInset = 80;
    t.cabinet()->desk->cableCutout = furniture::DeskCableCutout{250, 50, 30, -100};
    ASSERT_TRUE(doc.execute(commands::addItem(std::move(t))).ok());
    const Project& p = doc.project();
    auto text = serializeProject(p, kSavedAt);
    ASSERT_TRUE(text.ok()) << text.error().message;
    auto loaded = parseProject(text.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_FALSE(core::hasErrors(loaded.value().diagnostics));
    const Project& q = loaded.value().project;
    ASSERT_EQ(q.items.size(), 2u);
    EXPECT_EQ(*q.items[0].cabinet()->desk, *p.items[0].cabinet()->desk);
    EXPECT_EQ(*q.items[1].cabinet()->desk, *p.items[1].cabinet()->desk);
    EXPECT_EQ(serializeProject(q, kSavedAt).value(), text.value());
    // Desk 4 parts + pedestal; table 1 part with 16 leg holes.
    const domain::Part* top = findPart(loaded.value().resolved, "table/top");
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->operations.size(), 16u);
    EXPECT_NE(findPart(loaded.value().resolved, "desk/pedestal/side.left"), nullptr);
    // Defaults are not written ("leg" / "legInset" of the desk, "modestyPanel" true).
    const formats::Json deskJson = toJson(*p.items[0].cabinet())["desk"];
    EXPECT_FALSE(deskJson.contains("leg"));
    EXPECT_FALSE(deskJson.contains("modestyPanel"));
    EXPECT_TRUE(deskJson.contains("pedestal"));
}

TEST(ProjectDesk, OrdinaryCabinetsHaveNoDeskKeyAndLegIsAddedOnce) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    EXPECT_EQ(serializeProject(p, kSavedAt).value().find("\"desk\""), std::string::npos);

    Document doc(newProject(meta()));
    const std::size_t before = doc.project().libraries.hardware.size();
    ASSERT_EQ(doc.project().libraries.findHardware(domain::HardwareId(furniture::kGenericLegId)), nullptr);
    ASSERT_TRUE(doc.execute(commands::addItem(templateItem(&furniture::templates::desk, "d"))).ok());
    EXPECT_EQ(doc.project().libraries.hardware.size(), before); // panel desk: no leg
    ASSERT_TRUE(doc.execute(commands::addItem(templateItem(&furniture::templates::table, "t1"))).ok());
    ASSERT_TRUE(doc.execute(commands::addItem(templateItem(&furniture::templates::table, "t2"))).ok());
    EXPECT_EQ(doc.project().libraries.hardware.size(), before + 1);
    auto r = doc.resolve();
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_FALSE(core::hasErrors(r.value().diagnostics));
    ASSERT_TRUE(doc.undo().ok());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.project().libraries.hardware.size(), before);
}

TEST(ProjectDesk, StrictDecoding) {
    furniture::CabinetDefinition d = furniture::templates::desk(domain::AssemblyId("d"), defaultTemplateMaterials());
    formats::Json j = toJson(d);
    j["desk"]["support"] = "Trestle";
    auto bad = cabinetFromJson(j, "$.items[0]");
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("Panels"), std::string::npos);
    j = toJson(d);
    j["desk"]["pedestal"]["drawers"] = 9;
    EXPECT_FALSE(cabinetFromJson(j, "$.items[0]").ok());
    j = toJson(d);
    j["desk"]["unknown"] = 1;
    EXPECT_FALSE(cabinetFromJson(j, "$.items[0]").ok());
}

} // namespace fos::project::test
