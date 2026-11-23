// SPDX-License-Identifier: AGPL-3.0-only
// `fos plinth`.
#include "cli_test_support.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/furniture/templates.hpp"
#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

/// Project with `count` base cabinets on feet, 600 wide side by side from x = 0, saved to `file`.
/// `feet` = false keeps the template's sides-to-floor plinth.
void writeKitchen(const std::string& file, int count, bool feet = true) {
    project::Metadata m;
    m.id = kTestUuid;
    m.name = "Kitchen";
    project::Project p = project::newProject(m);
    for (int i = 0; i < count; ++i) {
        project::Item item;
        item.definition = furniture::templates::baseCabinet(domain::AssemblyId("cab" + std::to_string(i + 1)),
                                                            project::defaultTemplateMaterials());
        if (feet) {
            furniture::ConstructionOptions o = item.cabinet()->construction;
            o.plinth = furniture::PlinthKind::Feet;
            ASSERT_TRUE(furniture::setConstruction(*item.cabinet(), p.libraries, o).ok());
        }
        item.cabinet()->placement = geometry::Transform::translation({600.0 * i, 0, 0});
        p.items.push_back(item);
    }
    ASSERT_TRUE(project::saveProject(u8path(file), p).ok());
}

TEST(CliPlinth, AddsPlinthWithReturns) {
    TempDir dir;
    const std::string in = dir.file("k.fosp"), out = dir.file("out.fosp");
    writeKitchen(in, 3);
    // Run 0 .. 1800; --max-length 1000 -> 2 equal pieces of 900.
    const auto r = fos({"plinth", in, "--items", "cab1,cab2,cab3", "--setback", "60", "--height", "90",
                        "--max-length", "1000", "--returns", "left,right", "-o", out});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Plinth under 3 cabinet(s) (cab1, cab2, cab3): run 1800 mm, height 90 mm")) << r.out;
    EXPECT_TRUE(contains(r.out, "2 front piece(s): plinth.1 (0..900) plinth.2 (900..1800).")) << r.out;
    EXPECT_TRUE(contains(r.out, "End return(s): plinth.left plinth.right.")) << r.out;
    auto loaded = project::loadProject(u8path(out));
    ASSERT_TRUE(loaded.ok());
    EXPECT_FALSE(core::hasErrors(loaded.value().resolved.diagnostics));
    EXPECT_EQ(loaded.value().project.items.size(), 7u); // 3 cabinets + 2 pieces + 2 returns
    EXPECT_EQ(project::loadProject(u8path(in)).value().project.items.size(), 3u); // input untouched
}

TEST(CliPlinth, RefusalsAndUsage) {
    TempDir dir;
    const std::string in = dir.file("k.fosp"), sides = dir.file("s.fosp");
    writeKitchen(in, 2);
    writeKitchen(sides, 1, false);
    const auto r = fos({"plinth", sides, "--items", "cab1"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "Feet")) << r.err;
    EXPECT_TRUE(contains(r.err, "Nothing was saved."));
    EXPECT_EQ(fos({"plinth", in, "--items", "cab1,zz"}).code, kExitValidationErrors);
    EXPECT_EQ(fos({"plinth", in}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"plinth", in, "--items", "cab1", "--returns", "front"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"plinth", in, "--items", "cab1", "--setback", "abc"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"plinth", "--help"}).code, kExitOk);
}

} // namespace
} // namespace fos::cli::test
