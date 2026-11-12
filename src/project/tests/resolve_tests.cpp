// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include "fos/furniture/parameters.hpp"

namespace fos::project::test {

TEST(NewProject, DefaultLibraries) {
    const Project p = newProject(meta());
    EXPECT_EQ(p.metadata.name, "Kitchen");
    ASSERT_EQ(p.libraries.materials.size(), 3u);
    EXPECT_EQ(p.libraries.materials[0].id.str(), defaults::kParticleboard18);
    EXPECT_DOUBLE_EQ(p.libraries.materials[2].thickness, 3.0);
    ASSERT_EQ(p.libraries.edgeBands.size(), 2u);
    EXPECT_DOUBLE_EQ(p.libraries.edgeBands[1].thickness, 0.4);
    EXPECT_EQ(p.libraries.hardware.size(), hardware::builtinCatalog().size());
    for (const auto& m : p.libraries.materials) {
        EXPECT_TRUE(m.manufacturer.empty()); // generic, not a product
        EXPECT_DOUBLE_EQ(m.costPerSheet, 0.0);
    }
    EXPECT_TRUE(p.items.empty());
}

TEST(Resolve, BaseCabinetTemplatePartSizes) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    const Resolved r = resolved(p);
    ASSERT_EQ(r.items.size(), 1u);
    EXPECT_TRUE(r.items[0].generated);
    ASSERT_EQ(r.model.assemblies.size(), 1u);
    EXPECT_EQ(r.model.materials.size(), 3u); // libraries copied in
    EXPECT_EQ(r.model.assemblies[0].id.str(), "cab1");
    // 600 x 820 x 560, T = 18: inner width 600 - 36 = 564; 10 parts:
    // 2 sides, bottom, 2 rails, plinth, 2 doors, shelf, back.
    EXPECT_EQ(r.model.assemblies[0].parts.size(), 10u);
    struct Row { const char* id; double l, w, t, cutL, cutW; };
    // Cut = finished - edge band: sides L1 abs2 (2), W1/W2 abs04 (0.4): 820 - 0.8, 560 - 2.
    // Doors: all four edges abs2: 716 - 4, 296.5 - 4. Door height = (820 - 100) - 2 * 2 reveal;
    // door width = (600 - 2 * 2 - 3 gap) / 2 = 296.5.
    const Row rows[] = {
        {"cab1/side.left", 820, 560, 18, 819.2, 558},  {"cab1/side.right", 820, 560, 18, 819.2, 558},
        {"cab1/bottom", 564, 560, 18, 564, 558},       {"cab1/rail.front", 564, 100, 18, 564, 98},
        {"cab1/rail.back", 564, 100, 18, 564, 100},    {"cab1/plinth", 564, 100, 18, 564, 100},
        {"cab1/door.left", 716, 296.5, 18, 712, 292.5}, {"cab1/shelf.1", 562, 540, 18, 562, 538},
        {"cab1/back", 596, 716, 3, 596, 716},
    };
    for (const auto& row : rows) {
        const auto* part = findPart(r, row.id);
        ASSERT_NE(part, nullptr) << row.id;
        EXPECT_DOUBLE_EQ(part->length, row.l) << row.id;
        EXPECT_DOUBLE_EQ(part->width, row.w) << row.id;
        EXPECT_DOUBLE_EQ(part->thickness, row.t) << row.id;
        const auto cut = domain::cutSize(*part, r.model);
        EXPECT_NEAR(cut.length, row.cutL, 1e-9) << row.id;
        EXPECT_NEAR(cut.width, row.cutW, 1e-9) << row.id;
    }
}

TEST(Resolve, PlacementAndPanelItem) {
    Project p = newProject(meta());
    Item cab = baseCabinetItem();
    cab.cabinet()->placement = geometry::Transform::translation({1000, 0, 0});
    p.items.push_back(cab);
    PanelItem panel;
    panel.id = domain::AssemblyId("worktop");
    panel.placement = geometry::Transform::translation({0, 0, 820});
    panel.panel = furniture::makePanel(domain::PartId("worktop/panel"), "Worktop", domain::MaterialId(defaults::kMdf18));
    p.items.push_back(Item{panel, formats::Json::object()});
    const Resolved r = resolved(p);
    ASSERT_EQ(r.model.assemblies.size(), 2u);
    EXPECT_DOUBLE_EQ(r.model.assemblies[0].placement.origin.x, 1000.0);
    EXPECT_EQ(r.model.assemblies[1].id.str(), "worktop");
    EXPECT_DOUBLE_EQ(r.model.assemblies[1].placement.origin.z, 820.0);
    ASSERT_EQ(r.model.assemblies[1].parts.size(), 1u);
    // makePanel: 600 x 300, thickness from MDF 18.
    EXPECT_DOUBLE_EQ(r.model.assemblies[1].parts[0].length, 600.0);
    EXPECT_DOUBLE_EQ(r.model.assemblies[1].parts[0].width, 300.0);
    EXPECT_DOUBLE_EQ(r.model.assemblies[1].parts[0].thickness, 18.0);
}

TEST(Resolve, GenerationErrorIsADiagnosticNotAFailure) {
    Project p = newProject(meta());
    Item bad = baseCabinetItem("bad");
    bad.cabinet()->materials.carcass = domain::MaterialId("mat.missing");
    p.items.push_back(bad);
    p.items.push_back(baseCabinetItem("good"));
    auto r = resolve(p);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().items.size(), 2u);
    EXPECT_FALSE(r.value().items[0].generated);
    EXPECT_TRUE(r.value().items[1].generated);
    ASSERT_EQ(r.value().model.assemblies.size(), 1u);
    EXPECT_EQ(r.value().model.assemblies[0].id.str(), "good");
    bool found = false;
    for (const auto& d : r.value().diagnostics)
        if (d.code == "project.item.generation_failed" && d.entityId == "bad") found = true;
    EXPECT_TRUE(found);
}

TEST(Resolve, StructuralErrors) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    p.items.push_back(baseCabinetItem());
    auto r = resolve(p);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("\"cab1\""), std::string::npos);
    Project q = newProject(meta());
    q.libraries.materials.push_back(q.libraries.materials[0]);
    EXPECT_FALSE(resolve(q).ok());
}

TEST(Resolve, Deterministic) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem("a"));
    p.items.push_back(baseCabinetItem("b"));
    const std::string first = modelText(resolved(p).model);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(modelText(resolved(p).model), first);
    auto s1 = serializeProject(p, "2026-11-12T10:00:00Z");
    auto s2 = serializeProject(p, "2026-11-12T10:00:00Z");
    ASSERT_TRUE(s1.ok());
    EXPECT_EQ(s1.value(), s2.value());
}

TEST(Outputs, RemnantsAndPerMaterialSettings) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    CuttingSettings hdf;
    hdf.kerf = 3.2;
    hdf.remnants.push_back({"r1", "Offcut shelf", 900, 800, 2});
    p.cutting.materials[defaults::kHdf3] = hdf;
    const CuttingPlans plans = cuttingPlans(resolved(p), p.cutting);
    ASSERT_EQ(plans.plans.size(), 2u);
    const auto& job = plans.plans[1].job;
    EXPECT_EQ(job.id, defaults::kHdf3);
    EXPECT_DOUBLE_EQ(job.kerf, 3.2);
    EXPECT_DOUBLE_EQ(plans.plans[0].job.kerf, 4.0); // default
    ASSERT_EQ(job.stock.size(), 2u);
    EXPECT_EQ(job.stock[1].id, "mat.generic.hdf3/remnant/r1");
    EXPECT_TRUE(job.stock[1].isRemnant);
    EXPECT_EQ(job.stock[1].quantity, 2);
    // The back (596 x 716) fits the 900 x 800 remnant minus 10 mm trim per side (880 x 780),
    // and remnants are opened first (priority 1): the plan uses the remnant, not a full sheet.
    ASSERT_TRUE(plans.plans[1].result.has_value());
    ASSERT_EQ(plans.plans[1].result->sheets.size(), 1u);
    EXPECT_EQ(plans.plans[1].result->sheets[0].stockId, "mat.generic.hdf3/remnant/r1");
    EXPECT_FALSE(core::hasErrors(plans.plans[1].validation));
}

TEST(Outputs, DrawingOfUnknownItem) {
    Project p = newProject(meta());
    auto d = assemblyDrawing(p, resolved(p), "nope");
    ASSERT_FALSE(d.ok());
    EXPECT_EQ(d.error().code, core::ErrorCode::NotFound);
}

} // namespace fos::project::test
