// SPDX-License-Identifier: AGPL-3.0-only
// fos batch new|add|report|cut|labels
#include "cli_test_support.hpp"

namespace fos::cli::test {
namespace {

TEST(CliBatch, NewAddReportCutLabels) {
    TempDir dir;
    ASSERT_EQ(newBaseCabinet(dir.file("kitchen.fosp")).code, 0);
    const std::string batch = dir.file("week.fosbatch");
    auto r = fos({"batch", "new", batch, "--name", "Week 47", "--date", "2026-11-22"});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(fos({"batch", "new", batch, "--name", "again"}).code, 2); // never overwrites

    r = fos({"batch", "add", batch, dir.file("kitchen.fosp"), "--qty", "2"});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "quantity 2")) << r.out;
    EXPECT_TRUE(contains(readFile(dir.path / "week.fosbatch"), "\"path\": \"kitchen.fosp\"")); // relative
    EXPECT_EQ(fos({"batch", "add", batch, dir.file("kitchen.fosp"), "--qty", "0"}).code, 2);

    r = fos({"batch", "report", batch, "--type", "cutlist", "--csv"});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "kitchen#1/")) << r.out;
    EXPECT_TRUE(contains(r.out, "kitchen#2/")) << r.out;

    r = fos({"batch", "report", batch, "--type", "summary"});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "TOTAL")) << r.out;
    EXPECT_EQ(fos({"batch", "report", batch, "--type", "hardware"}).code, 0);
    EXPECT_EQ(fos({"batch", "report", batch, "--type", "nonsense"}).code, 2);

    r = fos({"batch", "cut", batch, "--out-dir", dir.file("cut")});
    ASSERT_EQ(r.code, 0) << r.out << r.err;
    EXPECT_TRUE(std::filesystem::exists(dir.path / "cut" / "cutting-summary.txt"));
    EXPECT_TRUE(contains(r.out, "validator (cutting::validateLayout): no findings")) << r.out;

    r = fos({"batch", "labels", batch, "--svg-dir", dir.file("labels")});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(readFile(dir.path / "labels" / "labels-1.svg"), "order kitchen#1"));
}

TEST(CliBatch, MissingProjectIsReportedButOthersAreProduced) {
    TempDir dir;
    ASSERT_EQ(newBaseCabinet(dir.file("a.fosp")).code, 0);
    ASSERT_EQ(newBaseCabinet(dir.file("b.fosp")).code, 0);
    const std::string batch = dir.file("x.fosbatch");
    ASSERT_EQ(fos({"batch", "new", batch, "--name", "X"}).code, 0);
    ASSERT_EQ(fos({"batch", "add", batch, dir.file("a.fosp")}).code, 0);
    ASSERT_EQ(fos({"batch", "add", batch, dir.file("b.fosp")}).code, 0);
    std::filesystem::remove(dir.path / "b.fosp");
    const auto r = fos({"batch", "report", batch, "--type", "cutlist"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.out, "INCOMPLETE BATCH")) << r.out;
    EXPECT_TRUE(contains(r.out, "batch.project.load_failed")) << r.out;
    EXPECT_TRUE(contains(r.out, "a/")) << r.out;
}

} // namespace
} // namespace fos::cli::test
