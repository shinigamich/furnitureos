// SPDX-License-Identifier: AGPL-3.0-only
// Hardware selection and per-part overrides (material, edge bands) through the Session.
#include "fos/app/cabinet_parts.hpp"
#include "fos/app/parameter_rows.hpp"
#include "fos/furniture/parameters.hpp"
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
    // The built-in catalogue has three generic hinges: full overlay, half overlay, inset.
    ASSERT_EQ(hinges.size(), 3u);
    EXPECT_EQ(hinges[0].id, hardware::ids::kHingeCup35);
    EXPECT_EQ(hinges[1].id, hardware::ids::kHingeCup35Half);
    EXPECT_EQ(hinges[2].id, hardware::ids::kHingeCup35Inset);
    EXPECT_FALSE(hinges[0].missing);
    EXPECT_EQ(hardwareKind(HardwareRole::ShelfPin), domain::HardwareKind::ShelfSupport);
    EXPECT_EQ(hardwareRoleTitle(HardwareRole::Confirmat), QStringLiteral("Confirmat screw:"));
    // An id that is not in the library is listed as missing so the editor can show it.
    const auto odd = hardwareChoices(s.project().libraries, HardwareRole::Hinge, "hw.gone");
    ASSERT_EQ(odd.size(), 4u);
    EXPECT_TRUE(odd[3].missing);

    // A second hinge in the library (copy of the generic one under a new id) becomes selectable.
    domain::HardwareDefinition copy =
        *s.project().libraries.findHardware(domain::HardwareId(hardware::ids::kHingeCup35));
    copy.id = domain::HardwareId("hw.test.hinge_b");
    copy.name = "Test hinge B";
    ASSERT_TRUE(s.execute(project::commands::upsertHardware(copy)).ok());
    ASSERT_EQ(hardwareChoices(s.project().libraries, HardwareRole::Hinge, "").size(), 4u);
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

const domain::HardwareInstance* instance(const Session& s, const std::string& definition) {
    for (const auto& h : s.resolved()->model.assemblies.at(0).hardware)
        if (h.definition.str() == definition)
            return &h;
    return nullptr;
}

TEST(CabinetHardware, HangerRailAndSlidingChoices) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::WallCabinet).ok());
    const auto& libs = s.project().libraries;
    // Hanger: "None" first (the default selection is empty), then the generic hanger.
    const auto hangers = hardwareChoices(libs, HardwareRole::Hanger, cabinet(s).hardware.hanger.str());
    ASSERT_EQ(hangers.size(), 2u);
    EXPECT_TRUE(hangers[0].none);
    EXPECT_TRUE(hangers[0].id.empty());
    EXPECT_EQ(hangers[1].id, hardware::ids::kHangerGeneric);
    EXPECT_TRUE(hardwareRoleOptional(HardwareRole::Hanger));
    EXPECT_FALSE(hardwareRoleOptional(HardwareRole::WallRail));
    // Wall rail: only kind-Other entries carrying the rail keys (clothes rails excluded).
    const auto rails = hardwareChoices(libs, HardwareRole::WallRail, cabinet(s).hardware.wallRail.str());
    ASSERT_EQ(rails.size(), 1u);
    EXPECT_EQ(rails[0].id, hardware::ids::kWallRailGeneric);
    const auto sliding = hardwareChoices(libs, HardwareRole::SlidingSystem, cabinet(s).hardware.slidingSystem.str());
    ASSERT_EQ(sliding.size(), 1u);
    EXPECT_EQ(sliding[0].id, hardware::ids::kSlidingGeneric);
    EXPECT_EQ(hardwareKind(HardwareRole::Hanger), domain::HardwareKind::Suspension);
    // Generic back_space_min = 18 mm (hardware README).
    EXPECT_NE(hangerHint(libs, cabinet(s).hardware).indexOf(QStringLiteral("18 mm")), -1)
        << hangerHint(libs, cabinet(s).hardware).toStdString();
}

TEST(CabinetHardware, HangerNeedsGroovedBackAndUndoes) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::WallCabinet).ok());
    const std::string id = cabinet(s).id.str();
    // The wall-cabinet template has an Overlay back: refused with the engine message naming
    // the required groove distance (18 mm); nothing changes.
    ASSERT_EQ(cabinet(s).construction.back, furniture::BackMount::Overlay);
    const auto revision = s.revision();
    auto refused = s.execute(hardwareCommand(cabinet(s), HardwareRole::Hanger, hardware::ids::kHangerGeneric));
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("Overlay"), std::string::npos) << refused.error().message;
    EXPECT_NE(refused.error().message.find("18"), std::string::npos) << refused.error().message;
    EXPECT_TRUE(cabinet(s).hardware.hanger.empty());
    EXPECT_EQ(s.revision(), revision);

    // Grooved back with groove_distance 18 (= back_space_min): accepted.
    furniture::ConstructionOptions o = cabinet(s).construction;
    o.back = furniture::BackMount::Grooved;
    ASSERT_TRUE(s.execute(project::commands::setConstruction(id, o)).ok());
    ParameterInput in;
    in.value = 18;
    ASSERT_TRUE(s.execute(parameterCommand(id, furniture::param::kGrooveDistance, in)).ok());
    EXPECT_EQ(instance(s, hardware::ids::kHangerGeneric), nullptr);
    ASSERT_TRUE(s.execute(hardwareCommand(cabinet(s), HardwareRole::Hanger, hardware::ids::kHangerGeneric)).ok());
    EXPECT_EQ(cabinet(s).hardware.hanger.str(), hardware::ids::kHangerGeneric);
    // One hanger per side (2 in total) and one wall rail.
    int hangers = 0;
    for (const auto& h : s.resolved()->model.assemblies.at(0).hardware)
        if (h.definition.str() == hardware::ids::kHangerGeneric)
            hangers += h.quantity;
    EXPECT_EQ(hangers, 2);
    const auto* rail = instance(s, hardware::ids::kWallRailGeneric);
    ASSERT_NE(rail, nullptr);
    EXPECT_EQ(rail->quantity, 1);

    // "None" removes them again; undo brings them back, undo once more removes them.
    ASSERT_TRUE(s.execute(hardwareCommand(cabinet(s), HardwareRole::Hanger, "")).ok());
    EXPECT_EQ(instance(s, hardware::ids::kHangerGeneric), nullptr);
    EXPECT_EQ(instance(s, hardware::ids::kWallRailGeneric), nullptr);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_NE(instance(s, hardware::ids::kHangerGeneric), nullptr);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_TRUE(cabinet(s).hardware.hanger.empty());
    EXPECT_EQ(instance(s, hardware::ids::kWallRailGeneric), nullptr);
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
