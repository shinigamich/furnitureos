// SPDX-License-Identifier: AGPL-3.0-only
// `fos worktop`.
#include "cli_test_support.hpp"

#include "fos/furniture/templates.hpp"
#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

/// Project with `count` base cabinets 600 wide side by side from x = 0, saved to `file`.
void writeKitchen(const std::string& file, int count) {
    project::Metadata m;
    m.id = kTestUuid;
    m.name = "Kitchen";
    project::Project p = project::newProject(m);
    for (int i = 0; i < count; ++i) {
        project::Item item;
        item.definition = furniture::templates::baseCabinet(domain::AssemblyId("cab" + std::to_string(i + 1)),
                                                            project::defaultTemplateMaterials());
        item.cabinet()->placement = geometry::Transform::translation({600.0 * i, 0, 0});
        p.items.push_back(item);
    }
    ASSERT_TRUE(project::saveProject(u8path(file), p).ok());
}

TEST(CliWorktop, AddsWorktopWithSinkAndHob) {
    TempDir dir;
    const std::string in = dir.file("k.fosp"), out = dir.file("out.fosp");
    writeKitchen(in, 3);
    // Run 0 .. 1800. Sink 400 .. 1000 (centre 700), hob 1150 .. 1650 (centre 1400): 150 mm apart,
    // 150 mm from the right end, 400 mm from the left end.
    const auto r = fos({"worktop", in, "--items", "cab1,cab2,cab3", "--overhang", "25", "--sink", "700:600:450:10",
                        "--hob", "1400:500:450", "-o", out});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Worktop over 3 cabinet(s) (cab1, cab2, cab3): 1800.0 x")) << r.out;
    EXPECT_TRUE(contains(r.out, "One piece: item worktop.")) << r.out;
    EXPECT_TRUE(contains(r.out, "Cut-out sink: 600.0 x 450.0 mm, centre 700.0 mm from the run start, R 10.0."));
    auto loaded = project::loadProject(u8path(out));
    ASSERT_TRUE(loaded.ok());
    EXPECT_FALSE(core::hasErrors(loaded.value().resolved.diagnostics));
    const project::Item* w = loaded.value().project.findItem("worktop");
    ASSERT_NE(w, nullptr);
    ASSERT_TRUE(w->panel()->panel.contour.has_value());
    EXPECT_EQ(w->panel()->panel.contour->cutouts.size(), 2u);
    // The input file is untouched (still 3 items).
    EXPECT_EQ(project::loadProject(u8path(in)).value().project.items.size(), 3u);

    // In place, a long run: 7 cabinets = 4200 > 4080 usable -> butt joint at 2100.
    const std::string longRun = dir.file("long.fosp");
    writeKitchen(longRun, 7);
    const auto r2 = fos({"worktop", longRun, "--items", "cab1,cab2,cab3,cab4,cab5,cab6,cab7"});
    ASSERT_EQ(r2.code, kExitOk) << r2.err;
    EXPECT_TRUE(contains(r2.out, "BUTT JOINTS at 2100.0 mm")) << r2.out;
    EXPECT_EQ(project::loadProject(u8path(longRun)).value().project.items.size(), 9u);
}

TEST(CliWorktop, RefusalsAndUsage) {
    TempDir dir;
    const std::string in = dir.file("k.fosp");
    writeKitchen(in, 2);
    // Sink 300 wide at centre 100 -> starts at x = -50: closer than 100 mm to the left end.
    const auto r = fos({"worktop", in, "--items", "cab1,cab2", "--sink", "100:300:400"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "rule of thumb")) << r.err;
    EXPECT_TRUE(contains(r.err, "Nothing was saved."));
    EXPECT_EQ(fos({"worktop", in, "--items", "cab1,zz"}).code, kExitValidationErrors);
    EXPECT_EQ(fos({"worktop", in}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"worktop", in, "--items", "cab1", "--sink", "1:2"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"worktop", in, "--items", "cab1", "--overhang", "abc"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"worktop", "--help"}).code, kExitOk);
}

} // namespace
} // namespace fos::cli::test
