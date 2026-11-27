// SPDX-License-Identifier: AGPL-3.0-only
#include "cli_test_support.hpp"

#include "fos/project/remnant_store.hpp"

#include <algorithm>

namespace fos::cli::test {
namespace {

project::RemnantStore load(const std::string& file) {
    auto s = project::loadRemnantStore(u8path(file));
    EXPECT_TRUE(s.ok()) << (s.ok() ? "" : s.error().message);
    return s.ok() ? s.value() : project::RemnantStore{};
}

} // namespace

TEST(CliRemnants, AddListRemove) {
    TempDir dir;
    const std::string store = dir.file("shop.fosremnants");
    auto r = fos({"remnants", "add", store, "--material", "oak.19", "--length", "1200", "--width", "400",
                  "--thickness", "19", "--grain", "length", "--location", "Rack 2", "--date", "2026-11-20"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "will be created"));
    EXPECT_TRUE(contains(r.out, "Added R0001  oak.19  1200 x 400 x 19 mm, grain length, added 2026-11-20, at Rack 2"))
        << r.out;
    r = fos({"remnants", "add", store, "--material", "white.18", "--length", "800", "--width", "300",
             "--thickness", "18", "--reserved-by", "Job 7", "--date", "2026-11-21"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Added R0002"));
    EXPECT_EQ(fos({"remnants", "add", store, "--material", "x", "--length", "-1", "--width", "3", "--thickness", "18"}).code,
              kExitUsageOrIo);
    EXPECT_EQ(fos({"remnants", "add", store, "--id", "R0001", "--material", "x", "--length", "1", "--width", "3",
                   "--thickness", "18"})
                  .code,
              kExitUsageOrIo); // duplicate id
    EXPECT_EQ(load(store).remnants.size(), 2u);

    r = fos({"remnants", "list", store});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    // 1200*400 + 800*300 = 480000 + 240000 = 720000 mm2 = 0.72 m2
    EXPECT_TRUE(contains(r.out, "2 remnant(s), 0.72 m2")) << r.out;
    EXPECT_TRUE(contains(r.out, "Job 7"));
    r = fos({"remnants", "list", store, "--material", "oak.19"});
    EXPECT_TRUE(contains(r.out, "1 remnant(s), 0.48 m2")) << r.out;

    r = fos({"remnants", "remove", store, "R0001", "R0099"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "'R0099' is not in the store; nothing was removed"));
    EXPECT_EQ(load(store).remnants.size(), 2u);
    r = fos({"remnants", "remove", store, "R0001"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Removed R0001  oak.19"));
    EXPECT_EQ(load(store).remnants.size(), 1u);
    EXPECT_TRUE(std::filesystem::exists(u8path(store + ".bak1"))); // atomic save with backups
    EXPECT_EQ(fos({"remnants", "list", dir.file("missing.fosremnants")}).code, kExitUsageOrIo);
}

TEST(CliRemnants, ImportOffcutsThenUseAndCommit) {
    TempDir dir;
    const std::string proj = dir.file("kitchen.fosp");
    ASSERT_EQ(newBaseCabinet(proj).code, kExitOk);
    const std::string store = dir.file("shop.fosremnants");

    auto r = fos({"remnants", "import-offcuts", store, proj, "--date", "2026-11-27", "--min-length", "300",
                  "--min-width", "100"});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "offcut(s) of at least 300 x 100 mm added.")) << r.out;
    const auto imported = load(store);
    ASSERT_FALSE(imported.remnants.empty());
    for (const auto& e : imported.remnants) {
        EXPECT_GE(std::max(e.length, e.width), 300.0);
        EXPECT_GE(std::min(e.length, e.width), 100.0);
        EXPECT_EQ(e.source, "offcuts of 'Kitchen' (kitchen.fosp)");
    }
    // Same project again: refused, store unchanged.
    r = fos({"remnants", "import-offcuts", store, proj});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "nothing was added"));
    EXPECT_EQ(load(store).remnants.size(), imported.remnants.size());

    // Dry run: prints the used remnants, store untouched.
    r = fos({"remnants", "use", store, proj, "--prefer-remnants"});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "USED R")) << r.out;
    EXPECT_TRUE(contains(r.out, "Store not changed"));
    EXPECT_EQ(load(store).remnants.size(), imported.remnants.size());

    r = fos({"remnants", "use", store, proj, "--prefer-remnants", "--commit", "--date", "2026-11-28",
             "--out-dir", dir.file("plans")});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Removed (used) R")) << r.out;
    EXPECT_TRUE(std::filesystem::exists(u8path(dir.file("plans")) / "cutting-summary.txt"));
    const auto after = load(store);
    // Every used remnant is gone, every other old entry is still there.
    std::size_t kept = 0, removed = 0;
    for (const auto& e : imported.remnants) {
        if (after.find(e.id) != nullptr) ++kept;
        else {
            ++removed;
            EXPECT_TRUE(contains(r.out, "Removed (used) " + e.id + " ")) << e.id;
        }
    }
    EXPECT_GT(removed, 0u);
    EXPECT_EQ(kept + removed, imported.remnants.size());
    std::size_t added = 0;
    for (const auto& e : after.remnants)
        if (e.dateAdded == "2026-11-28") ++added;
    EXPECT_EQ(after.remnants.size(), kept + added);
    EXPECT_TRUE(contains(r.out, std::to_string(removed) + " remnant(s) removed, " + std::to_string(added) +
                                    " offcut(s) added."))
        << r.out;
}

} // namespace fos::cli::test
