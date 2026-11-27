// SPDX-License-Identifier: AGPL-3.0-only
// Workshop offcut stock in the cutting workspace: offered / skipped / used rows, planned stock
// change after cutting, file round trip (project/remnant_use.hpp decides; the app presents).
#include "fos/app/templates.hpp"
#include "fos/app/workshop_offcuts.hpp"
#include "test_support.hpp"

#include <algorithm>

namespace fos::app::test {
namespace {
project::StoredRemnant remnant(std::string id, std::string material, double l, double w, double t,
                               std::string reservedBy = {}) {
    project::StoredRemnant r;
    r.id = std::move(id);
    r.materialId = std::move(material);
    r.length = l;
    r.width = w;
    r.thickness = t;
    r.dateAdded = "2026-11-01";
    r.reservedBy = std::move(reservedBy);
    return r;
}
const WorkshopRemnantRow* row(const std::vector<WorkshopRemnantRow>& rows, const std::string& id) {
    for (const auto& r : rows)
        if (r.id == id) return &r;
    return nullptr;
}
} // namespace

TEST(WorkshopOffcuts, OfferedSkippedUsedAndStockChange) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    ASSERT_NE(s.resolved(), nullptr);
    const furniture::CabinetDefinition& cab = *s.project().items[0].cabinet();
    const std::string carcass = cab.materials.carcass.str();
    const double t = s.project().libraries.findMaterial(cab.materials.carcass)->thickness; // 18

    project::RemnantStore store;
    // R0001: same material and thickness, free, large enough for a side (820 x 560) -> offered.
    store.remnants.push_back(remnant("R0001", carcass, 1200, 900, t));
    // R0002: other thickness -> skipped; R0003: reserved by another job -> skipped.
    store.remnants.push_back(remnant("R0002", carcass, 1200, 900, t - 2));
    store.remnants.push_back(remnant("R0003", carcass, 1200, 900, t, "Other job"));
    // R0004: a material the project does not cut -> not listed at all.
    store.remnants.push_back(remnant("R0004", "no.such.board", 1200, 900, t));

    const project::StoreCuttingRun run = workshopCuttingRun(*s.resolved(), s.project(), store);
    EXPECT_EQ(run.offered, std::vector<std::string>{"R0001"});
    const auto rows = workshopRemnantRows(store, run);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(row(rows, "R0004"), nullptr);
    ASSERT_NE(row(rows, "R0002"), nullptr);
    EXPECT_EQ(row(rows, "R0002")->status, WorkshopRemnantRow::Status::Skipped);
    EXPECT_FALSE(row(rows, "R0002")->detail.isEmpty());
    EXPECT_EQ(row(rows, "R0003")->status, WorkshopRemnantRow::Status::Skipped);
    const WorkshopRemnantRow* r1 = row(rows, "R0001");
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->size, QStringLiteral("1200 × 900 × %1 mm").arg(t));
    const bool used = !run.used.empty();
    EXPECT_EQ(r1->status, used ? WorkshopRemnantRow::Status::Used : WorkshopRemnantRow::Status::OfferedUnused);
    EXPECT_EQ(workshopRunSummary(run), QStringLiteral("1 offcuts offered, %1 used, 2 skipped").arg(used ? 1 : 0));

    // Planned stock change: used remnants removed, new offcuts added with today's date.
    const auto options = offcutImportOptions(s.project(), "/work/kitchen.fosp", QDate(2026, 11, 27));
    EXPECT_EQ(options.date, "2026-11-27");
    EXPECT_EQ(options.source, "offcut of '" + s.project().metadata.name + "' (kitchen.fosp)");
    EXPECT_EQ(offcutImportOptions(s.project(), {}, QDate(2026, 1, 2)).source,
              "offcut of '" + s.project().metadata.name + "'");
    auto commit = project::commitStoreUse(store, run, s.resolved()->model, options);
    ASSERT_TRUE(commit.ok()) << commit.error().message;
    EXPECT_EQ(commit.value().removed.size(), used ? 1u : 0u);
    const QStringList lines = storeChangeLines(commit.value());
    EXPECT_EQ(static_cast<std::size_t>(lines.size()), commit.value().removed.size() + commit.value().added.size());
    if (used) {
        EXPECT_TRUE(lines[0].startsWith(QStringLiteral("Remove (used): R0001")));
    }
    for (const auto& a : commit.value().added) EXPECT_EQ(a.dateAdded, "2026-11-27");

    // Without store remnants: nothing removed; the full-sheet layout leaves usable offcuts
    // (a 600 mm base cabinet uses a small part of a 2800 x 2070 sheet), numbered from R0001.
    auto plain = project::commitStoreUse(project::RemnantStore{}, plainCuttingRun(s.cuttingPlans()),
                                         s.resolved()->model, options);
    ASSERT_TRUE(plain.ok()) << plain.error().message;
    EXPECT_TRUE(plain.value().removed.empty());
    ASSERT_FALSE(plain.value().added.empty());
    EXPECT_EQ(plain.value().added[0].id, "R0001");
    EXPECT_TRUE(storeChangeLines(plain.value())[0].startsWith(QStringLiteral("Add: R0001")));

    // File: a missing stock file is an empty store only when allowed; save + load round trip.
    TempDir dir;
    const auto file = dir.path / "workshop.fosremnants";
    EXPECT_FALSE(loadWorkshopStore(file, false).ok());
    auto empty = loadWorkshopStore(file, true);
    ASSERT_TRUE(empty.ok());
    EXPECT_TRUE(empty.value().remnants.empty());
    ASSERT_TRUE(project::saveRemnantStore(file, plain.value().store).ok());
    auto back = loadWorkshopStore(file, false);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(back.value().remnants.size(), plain.value().added.size());
}

} // namespace fos::app::test
