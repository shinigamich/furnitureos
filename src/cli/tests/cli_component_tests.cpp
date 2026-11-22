// SPDX-License-Identifier: AGPL-3.0-only
#include "cli_test_support.hpp"

#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

TEST(CliComponent, SaveListInsertRoundTrip) {
    TempDir dir;
    const auto src = dir.file("src.fosp"), dst = dir.file("dst.fosp"), comp = dir.file("lib/base.foscomp");
    std::filesystem::create_directories(dir.path / "lib");
    ASSERT_EQ(fos({"new", src, "--template", "base", "--id", "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50"}).code, kExitOk);
    ASSERT_EQ(fos({"new", dst, "--id", "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f51"}).code, kExitOk);

    auto r = fos({"component", "save", src, "cab1", comp, "--name", "Base 600", "--category", "Base", "--tags", "kitchen,base"});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Saved component 'Base 600' (comp.base-600)")) << r.out;
    // Refuses to overwrite without --force.
    EXPECT_EQ(fos({"component", "save", src, "cab1", comp, "--name", "X"}).code, kExitUsageOrIo);

    r = fos({"component", "list", dir.file("lib")});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Base\n  Base 600  [comp.base-600]  cabinet  600 x 820 x")) << r.out;
    EXPECT_TRUE(contains(r.out, "tags: kitchen base")) << r.out;

    r = fos({"component", "insert", dst, comp, "Base 600", "--id", "k1", "--at", "1200,0,0"});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    auto loaded = project::loadProject(u8path(dst));
    ASSERT_TRUE(loaded.ok());
    ASSERT_NE(loaded.value().project.findItem("k1"), nullptr);
    EXPECT_EQ(loaded.value().project.findItem("k1")->placement().applyPoint({0, 0, 0}).x, 1200);

    // Unknown component name, bad --at, malformed file.
    EXPECT_EQ(fos({"component", "insert", dst, comp, "Nope"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"component", "insert", dst, comp, "Base 600", "--at", "1,2"}).code, kExitUsageOrIo);
    { std::ofstream(dir.file("lib/bad.foscomp")) << "{}"; }
    r = fos({"component", "list", dir.file("lib")});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "bad.foscomp")) << r.err;
}

TEST(CliComponent, NewWithBoxedDrawerTemplate) {
    TempDir dir;
    auto r = fos({"new", dir.file("d.fosp"), "--template", "drawers-boxed"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    auto loaded = project::loadProject(u8path(dir.file("d.fosp")));
    ASSERT_TRUE(loaded.ok());
    const auto* cab = loaded.value().project.findItem("cab1")->cabinet();
    ASSERT_NE(cab, nullptr);
    EXPECT_EQ(cab->name, "Drawer unit with boxes");
}

} // namespace
} // namespace fos::cli::test
