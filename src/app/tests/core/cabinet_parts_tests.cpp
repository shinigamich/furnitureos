// SPDX-License-Identifier: AGPL-3.0-only
// Hardware selection and per-part overrides (material, edge bands) through the Session.
#include "fos/app/cabinet_parts.hpp"
#include "fos/hardware/catalog.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

using domain::EdgeSide;

const furniture::CabinetDefinition& cabinet(const Session& s) {
    return *s.project().items.at(0).cabinet();
}

int hingesWith(const Session& s, const std::string& definition) {
    int n = 0;
    for (const auto& h : s.resolved()->model.assemblies.at(0).hardware)
        if (h.definition.str() == definition)
            n += h.quantity;
    return n;
}

TEST(CabinetHardware, ChoicesFromTheLibraryAndSelection) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto hinges =
        hardwareChoices(s.project().libraries, HardwareRole::Hinge, cabinet(s).hardware.hinge.str());
    ASSERT_EQ(hinges.size(), 1u); // the built-in catalogue has one hinge
    EXPECT_EQ(hinges[0].id, hardware::ids::kHingeCup35);
    EXPECT_FALSE(hinges[0].missing);
    EXPECT_EQ(hardwareKind(HardwareRole::ShelfPin), domain::HardwareKind::ShelfSupport);
    EXPECT_EQ(hardwareRoleTitle(HardwareRole::Confirmat), QStringLiteral("Confirmat screw:"));
    // An id that is not in the library is listed as missing so the editor can show it.
    const auto odd = hardwareChoices(s.project().libraries, HardwareRole::Hinge, "hw.gone");
    ASSERT_EQ(odd.size(), 2u);
    EXPECT_TRUE(odd[1].missing);

    // A second hinge in the library (copy of the generic one under a new id) becomes selectable.
    domain::HardwareDefinition copy =
        *s.project().libraries.findHardware(domain::HardwareId(hardware::ids::kHingeCup35));
    copy.id = domain::HardwareId("hw.test.hinge_b");
    copy.name = "Test hinge B";
    ASSERT_TRUE(s.execute(project::commands::upsertHardware(copy)).ok());
    ASSERT_EQ(hardwareChoices(s.project().libraries, HardwareRole::Hinge, "").size(), 2u);
    const int before = hingesWith(s, hardware::ids::kHingeCup35);
    EXPECT_GT(before, 0);
    ASSERT_TRUE(s.execute(hardwareCommand(cabinet(s), HardwareRole::Hinge, "hw.test.hinge_b")).ok());
    EXPECT_EQ(cabinet(s).hardware.hinge.str(), "hw.test.hinge_b");
    EXPECT_EQ(hingesWith(s, "hw.test.hinge_b"), before); // same positions, new definition
    EXPECT_EQ(hingesWith(s, hardware::ids::kHingeCup35), 0);
    EXPECT_EQ(cabinet(s).hardware.handle.str(), hardware::ids::kHandleBar128); // others unchanged

    // An id that is not in the library is accepted by the furniture module, which reports it
    // as a generation finding naming the id (the editor only offers library entries).
    ASSERT_TRUE(s.execute(hardwareCommand(cabinet(s), HardwareRole::Hinge, "hw.nope")).ok());
    bool reported = false;
    for (const auto& d : s.resolved()->diagnostics)
        reported = reported || d.message.find("hw.nope") != std::string::npos;
    EXPECT_TRUE(reported);
    ASSERT_TRUE(s.undo().ok());

    // Refused: the cabinet is no longer in the project (e.g. deleted meanwhile). Nothing changes.
    const auto hw = cabinet(s).hardware;
    const auto revision = s.revision();
    furniture::CabinetDefinition gone = cabinet(s);
    gone.id = domain::AssemblyId("cab9");
    auto r = s.execute(hardwareCommand(gone, HardwareRole::Hinge, hardware::ids::kHingeCup35));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("cab9"), std::string::npos) << r.error().message;
    EXPECT_EQ(cabinet(s).hardware, hw);
    EXPECT_EQ(s.revision(), revision);

    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(cabinet(s).hardware.hinge.str(), hardware::ids::kHingeCup35);
    ASSERT_TRUE(s.redo().ok());
    EXPECT_EQ(cabinet(s).hardware.hinge.str(), "hw.test.hinge_b");
}

TEST(PartOverride, SlotMaterialEdgesClearUndo) {
    EXPECT_EQ(partSlot("cab1", "cab1/side.left"), std::optional<std::string>("side.left"));
    EXPECT_EQ(partSlot("cab1", "cab10/side.left"), std::nullopt);
    EXPECT_EQ(partSlot("cab1", "cab1/"), std::nullopt);

    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto* shelf = part(s, "cab1/shelf.1");
    ASSERT_NE(shelf, nullptr);
    EXPECT_EQ(shelf->material.str(), project::defaults::kParticleboard18);
    ASSERT_TRUE(shelf->edge(EdgeSide::L1).has_value()); // front edge banded (visible edge)

    ASSERT_TRUE(
        s.execute(partMaterialCommand(cabinet(s), "shelf.1", std::string(project::defaults::kMdf18))).ok());
    EXPECT_EQ(part(s, "cab1/shelf.1")->material.str(), project::defaults::kMdf18);
    EXPECT_EQ(part(s, "cab1/side.left")->material.str(), project::defaults::kParticleboard18);

    // Raw front edge, thin band on W1; the material override is kept.
    ASSERT_TRUE(
        s.execute(partEdgeCommand(cabinet(s), "shelf.1", EdgeSide::L1, {EdgeOverrideChoice::Kind::Raw, {}}))
            .ok());
    ASSERT_TRUE(s.execute(partEdgeCommand(cabinet(s), "shelf.1", EdgeSide::W1,
                                          {EdgeOverrideChoice::Kind::Band, project::defaults::kAbs04}))
                    .ok());
    const auto* p = part(s, "cab1/shelf.1");
    EXPECT_FALSE(p->edge(EdgeSide::L1).has_value());
    ASSERT_TRUE(p->edge(EdgeSide::W1).has_value());
    EXPECT_EQ(p->edge(EdgeSide::W1)->str(), project::defaults::kAbs04);
    EXPECT_EQ(p->material.str(), project::defaults::kMdf18);
    const auto o = currentOverride(cabinet(s), "shelf.1");
    EXPECT_EQ(edgeChoice(o, EdgeSide::L1).kind, EdgeOverrideChoice::Kind::Raw);
    EXPECT_EQ(edgeChoice(o, EdgeSide::W1),
              (EdgeOverrideChoice{EdgeOverrideChoice::Kind::Band, project::defaults::kAbs04}));
    EXPECT_EQ(edgeChoice(o, EdgeSide::L2).kind, EdgeOverrideChoice::Kind::CabinetRule);

    // Refused: a material that is not in the library. Nothing changes.
    const auto materials = cabinet(s).materials;
    const auto revision = s.revision();
    auto refused = s.execute(partMaterialCommand(cabinet(s), "shelf.1", std::string("mat.nope")));
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("mat.nope"), std::string::npos) << refused.error().message;
    EXPECT_EQ(cabinet(s).materials, materials);
    EXPECT_EQ(s.revision(), revision);
    // An edge band missing from the library is accepted with the warning
    // furniture.edge.missing_band and the edge left raw (furniture generate.cpp).
    ASSERT_TRUE(s.execute(partEdgeCommand(cabinet(s), "shelf.1", EdgeSide::L2,
                                          {EdgeOverrideChoice::Kind::Band, "eb.nope"}))
                    .ok());
    EXPECT_FALSE(part(s, "cab1/shelf.1")->edge(EdgeSide::L2).has_value());
    bool warned = false;
    for (const auto& d : s.resolved()->diagnostics)
        warned = warned || d.code == "furniture.edge.missing_band";
    EXPECT_TRUE(warned);
    ASSERT_TRUE(s.undo().ok());

    // Setting everything back to the cabinet rule removes the override entry.
    ASSERT_TRUE(s.execute(partMaterialCommand(cabinet(s), "shelf.1", std::nullopt)).ok());
    ASSERT_TRUE(s.execute(partEdgeCommand(cabinet(s), "shelf.1", EdgeSide::L1, {})).ok());
    ASSERT_TRUE(s.execute(partEdgeCommand(cabinet(s), "shelf.1", EdgeSide::W1, {})).ok());
    EXPECT_EQ(cabinet(s).materials.overrides.count("shelf.1"), 0u);
    ASSERT_TRUE(part(s, "cab1/shelf.1")->edge(EdgeSide::L1).has_value());

    // Undo three steps -> the full override again; clear removes it in one step.
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(part(s, "cab1/shelf.1")->material.str(), project::defaults::kMdf18);
    ASSERT_TRUE(s.execute(clearPartOverrideCommand(cabinet(s), "shelf.1")).ok());
    EXPECT_EQ(part(s, "cab1/shelf.1")->material.str(), project::defaults::kParticleboard18);
    EXPECT_TRUE(part(s, "cab1/shelf.1")->edge(EdgeSide::L1).has_value());
}

} // namespace
} // namespace fos::app::test
