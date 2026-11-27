// SPDX-License-Identifier: AGPL-3.0-only
// Workshop remnant store (.fosremnants): codec, file round trip, offcut import, use/commit.
#include "test_support.hpp"

#include "fos/project/remnant_use.hpp"

namespace fos::project::test {
namespace {

StoredRemnant remnant(const std::string& id, const std::string& material, double l, double w, double t) {
    StoredRemnant r;
    r.id = id;
    r.materialId = material;
    r.length = l;
    r.width = w;
    r.thickness = t;
    r.dateAdded = "2026-11-20";
    return r;
}

} // namespace

TEST(RemnantStore, FileRoundTripKeepsEveryFieldAndUnknownPayloadKeys) {
    TempDir dir;
    RemnantStore store;
    StoredRemnant a = remnant("R0001", "white.18", 1200, 800, 18);
    a.grain = RemnantGrain::AlongWidth;
    a.location = "Rack 3";
    a.note = "Small chip on one corner";
    a.reservedBy = "Job 42";
    a.source = "offcut of 'Kitchen'";
    store.remnants = {a, remnant("R0002", "oak.19", 600, 300, 19)};
    store.extra = formats::Json{{"shop", "North"}};
    const auto path = dir.path / "shop.fosremnants";
    auto saved = saveRemnantStore(path, store, fixedClock());
    ASSERT_TRUE(saved.ok()) << saved.error().message;
    auto loaded = loadRemnantStore(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const RemnantStore& b = loaded.value();
    ASSERT_EQ(b.remnants.size(), 2u);
    EXPECT_EQ(b.remnants[0].grain, RemnantGrain::AlongWidth);
    EXPECT_EQ(b.remnants[0].location, "Rack 3");
    EXPECT_EQ(b.remnants[0].note, "Small chip on one corner");
    EXPECT_EQ(b.remnants[0].reservedBy, "Job 42");
    EXPECT_EQ(b.remnants[0].source, "offcut of 'Kitchen'");
    EXPECT_DOUBLE_EQ(b.remnants[1].thickness, 19.0);
    EXPECT_EQ(b.remnants[1].grain, RemnantGrain::None);
    EXPECT_EQ(b.extra.value("shop", ""), "North");
    EXPECT_EQ(remnantStoreToJson(b), remnantStoreToJson(store));
    EXPECT_EQ(nextRemnantId(b), "R0003");
    EXPECT_EQ(nextRemnantId(RemnantStore{}), "R0001");
}

TEST(RemnantStore, DecodeAndValueErrorsNameThePath) {
    const auto decode = [](const std::string& entry) {
        return remnantStoreFromJson(formats::Json::parse("{\"remnants\": [" + entry + "]}"));
    };
    const std::string good = R"({"id":"a","material":"m","length":10,"width":5,"thickness":18,"dateAdded":"2026-02-28")";
    EXPECT_TRUE(decode(good + "}").ok());
    auto r = decode(R"({"id":"a","material":"m","length":-5,"width":5,"thickness":18,"dateAdded":"2026-02-28"})");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("/payload/remnants/0/length"), std::string::npos) << r.error().message;
    r = decode(good + R"(,"grain":"diagonal"})");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("grain"), std::string::npos);
    r = decode(good + R"(,"colour":"red"})"); // unknown key
    EXPECT_FALSE(r.ok());
    r = decode(R"({"id":"a","material":"m","length":10,"width":5,"thickness":18,"dateAdded":"2026-02-30"})");
    ASSERT_FALSE(r.ok()); // 30 February
    EXPECT_NE(r.error().message.find("dateAdded"), std::string::npos);
    r = decode(good + "}," + good + "}");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("duplicate remnant id \"a\""), std::string::npos);
    EXPECT_TRUE(isIsoDate("2024-02-29"));
    EXPECT_FALSE(isIsoDate("2023-02-29"));

    RemnantStore bad;
    bad.remnants = {remnant("x", "", 0, 5, 18)};
    const auto d = validateRemnantStore(bad);
    EXPECT_EQ(d.size(), 2u); // no material, length 0
    TempDir dir;
    EXPECT_FALSE(saveRemnantStore(dir.path / "bad.fosremnants", bad).ok());
    EXPECT_FALSE(std::filesystem::exists(dir.path / "bad.fosremnants"));
}

// One piece 1500 x 800 (no rotation) on a 2000 x 1000 sheet, kerf 4, no trim: offcuts 496 x 1000
// at x 1504 and 1500 x 196 at y 804 (hand-checked in cutting/tests/offcut_tests.cpp).
TEST(RemnantStore, OffcutsBecomeEntriesWithGrainAndContinuingIds) {
    cutting::Job job;
    job.id = "oak";
    job.name = "Oak";
    job.thickness = 19;
    job.kerf = 4;
    job.stock = {{"oak/sheet", "Oak board", 2000, 1000, cutting::kUnlimited, 0, false, 0}};
    job.pieces = {{"p", "Door", 1500, 800, 1, false, {}}};
    CuttingPlans plans;
    CuttingPlan plan;
    plan.job = job;
    auto res = cutting::optimize(job);
    ASSERT_TRUE(res.ok());
    plan.result = res.value();
    plans.plans.push_back(plan);
    domain::Model model;
    domain::Material oak;
    oak.id = domain::MaterialId("oak");
    oak.hasGrain = true;
    model.materials.push_back(oak);

    RemnantStore store;
    store.remnants = {remnant("R0005", "oak", 700, 400, 19)};
    OffcutImportOptions o;
    o.date = "2026-11-27";
    o.location = "Offcut rack";
    o.source = "test";
    auto added = remnantsFromOffcuts(plans, model, store, o);
    ASSERT_EQ(added.size(), 2u);
    EXPECT_EQ(added[0].id, "R0006");
    EXPECT_EQ(added[1].id, "R0007");
    EXPECT_DOUBLE_EQ(added[0].length, 496);
    EXPECT_DOUBLE_EQ(added[0].width, 1000);
    EXPECT_DOUBLE_EQ(added[1].length, 1500);
    EXPECT_DOUBLE_EQ(added[1].width, 196);
    EXPECT_DOUBLE_EQ(added[0].thickness, 19);
    EXPECT_EQ(added[0].grain, RemnantGrain::AlongLength);
    EXPECT_EQ(added[0].location, "Offcut rack");
    EXPECT_EQ(added[0].note, "offcut of sheet 1 (oak/sheet) at x 1504, y 0");
    o.minWidth = 200; // the 196 mm strip is no longer kept
    EXPECT_EQ(remnantsFromOffcuts(plans, model, store, o).size(), 1u);
}

TEST(RemnantStore, UseOffersMatchingRemnantsFirstAndCommitUpdatesTheStore) {
    Project p = newProject(meta());
    p.items.push_back(baseCabinetItem());
    const Resolved r = resolved(p);
    const CuttingPlans plain = cuttingPlans(r, p.cutting);
    ASSERT_FALSE(plain.plans.empty());
    const cutting::Job& first = plain.plans.front().job;
    const std::string mat = first.id;
    const double t = first.thickness;

    RemnantStore store;
    store.remnants = {remnant("R0001", mat, 1200, 800, t),           // usable
                      remnant("R0002", mat, 1200, 800, t + 1),       // wrong thickness
                      remnant("R0003", mat, 1200, 800, t),           // reserved by another job
                      remnant("R0004", "no-such-material", 900, 900, t)}; // other material: ignored
    store.remnants[2].reservedBy = "Other job";
    StoreUseOptions uo;
    uo.jobName = "Kitchen";
    uo.preferRemnants = true;
    const StoreCuttingRun run = cuttingPlansWithStore(r, p.cutting, store, uo);
    EXPECT_FALSE(core::hasErrors(run.plans.diagnostics));
    ASSERT_EQ(run.offered, std::vector<std::string>{"R0001"});
    ASSERT_EQ(run.skipped.size(), 2u);
    EXPECT_EQ(run.skipped[0].remnantId, "R0002");
    EXPECT_EQ(run.skipped[1].remnantId, "R0003");
    EXPECT_NE(run.skipped[1].reason.find("reserved by 'Other job'"), std::string::npos);
    ASSERT_EQ(run.used.size(), 1u);
    EXPECT_EQ(run.used[0].remnantId, "R0001");
    EXPECT_EQ(run.used[0].materialId, mat);
    EXPECT_GT(run.used[0].pieceCount, 0);
    const auto* plan = run.plans.guillotineFor(mat);
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->result->sheets[static_cast<std::size_t>(run.used[0].sheetIndex)].stockId,
              mat + "/remnant/store:R0001");

    OffcutImportOptions o;
    o.date = "2026-11-27";
    auto commit = commitStoreUse(store, run, r.model, o);
    ASSERT_TRUE(commit.ok()) << commit.error().message;
    const StoreCommit& c = commit.value();
    ASSERT_EQ(c.removed.size(), 1u);
    EXPECT_EQ(c.removed[0].id, "R0001");
    EXPECT_FALSE(c.added.empty());
    EXPECT_EQ(c.added.front().id, "R0005");
    // Nothing lost: old 4 - 1 used + new offcuts.
    EXPECT_EQ(c.store.remnants.size(), 3u + c.added.size());
    EXPECT_EQ(c.store.find("R0001"), nullptr);
    EXPECT_NE(c.store.find("R0003"), nullptr);
    EXPECT_TRUE(validateRemnantStore(c.store).empty());

    // A plan error refuses the commit.
    StoreCuttingRun broken = run;
    broken.plans.diagnostics.push_back({core::Severity::Error, "project.cutting.layout_invalid", "x", mat});
    EXPECT_FALSE(commitStoreUse(store, broken, r.model, o).ok());
}

} // namespace fos::project::test
