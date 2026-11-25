// SPDX-License-Identifier: AGPL-3.0-only
#include "cli_test_support.hpp"

#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

TEST(CliMerge, ImportsRenamesAndRefusesOnError) {
    TempDir dir;
    const auto dst = dir.file("dst.fosp"), src = dir.file("src.fosp"), out = dir.file("out.fosp");
    ASSERT_EQ(fos({"new", dst, "--template", "base", "--id", "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f51"}).code, kExitOk);
    ASSERT_EQ(fos({"new", src, "--template", "base", "--id", "0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50"}).code, kExitOk);
    const std::string dstBefore = readFile(u8path(dst));

    // Errors: nothing saved, target untouched, no output file.
    auto r = fos({"merge", dst, src, "--items", "nope", "-o", out});
    EXPECT_EQ(r.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(r.err, "Nothing was saved")) << r.err;
    EXPECT_FALSE(std::filesystem::exists(u8path(out)));
    EXPECT_EQ(fos({"merge", dst, src, "--policy", "maybe"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"merge", dst, src, "--offset", "1,2"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"merge", dst}).code, kExitUsageOrIo);
    EXPECT_EQ(readFile(u8path(dst)), dstBefore);

    // Both have cab1: the imported one becomes cab2, moved by the offset, written to -o only.
    r = fos({"merge", dst, src, "--items", "cab1", "--policy", "rename", "--offset", "700,0,0", "-o", out});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Imported cab1 as cab2")) << r.out;
    EXPECT_EQ(readFile(u8path(dst)), dstBefore);
    auto loaded = project::loadProject(u8path(out));
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const auto& p = loaded.value().project;
    ASSERT_EQ(p.items.size(), 2u);
    ASSERT_NE(p.findItem("cab2"), nullptr);
    EXPECT_EQ(p.findItem("cab2")->placement().applyPoint({0, 0, 0}).x, 700);
    EXPECT_FALSE(contains(r.out, "Renamed")) << r.out;

    // Without -o the target itself is updated.
    ASSERT_EQ(fos({"merge", dst, src}).code, kExitOk);
    auto again = project::loadProject(u8path(dst));
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(again.value().project.items.size(), 2u);
}

} // namespace
} // namespace fos::cli::test
