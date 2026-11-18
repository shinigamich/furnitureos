// SPDX-License-Identifier: AGPL-3.0-only
// `fos cut` and `fos cam` on contoured (shaped) parts. The reference project (newBaseCabinet,
// W 800: shelf 762 x 540, front edge L1 banded ABS 2) gets R 30 front corners on its shelf through
// the project command (the CLI has no contour editing), so:
//   particleboard: 8 rectangular pieces sawn + the shelf nested; HDF back sawn;
//   nesting defaults: part spacing 8 + 0 + 2 * 0.1 = 8.2 mm, edge spacing 8.1 mm;
//   CAM blank with the default allowance 10: (762 + 20) x (540 + 20) = 782 x 560 mm.
#include "cli_test_support.hpp"

#include "fos/project/document.hpp"
#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

class CliShaped : public ::testing::Test {
protected:
    TempDir dir;
    std::string project = dir.file("shaped.fosp");
    void SetUp() override {
        ASSERT_EQ(newBaseCabinet(project).code, kExitOk);
        auto loaded = project::loadProject(u8path(project));
        ASSERT_TRUE(loaded.ok());
        project::Document doc(loaded.value().project);
        furniture::ContourSpec front;
        front.kind = furniture::ContourKind::RoundedCorners;
        front.radii = {"30", "30", "", ""};
        ASSERT_TRUE(doc.execute(project::commands::setPartContour("cab1", "shelf.1", front)).ok());
        ASSERT_TRUE(project::saveProject(u8path(project), doc.project()).ok());
    }
};

TEST_F(CliShaped, CutWritesGuillotineAndNestingSheets) {
    const std::string out = dir.file("cut");
    const auto r = fos({"cut", project, "--out-dir", out});
    ASSERT_EQ(r.code, kExitOk) << r.err << r.out;
    EXPECT_TRUE(contains(r.out, "Shaped parts are nested by a true-shape HEURISTIC"));
    EXPECT_TRUE(contains(r.out, "pieces: 8 required, 8 placed, 0 unplaced"));
    EXPECT_TRUE(contains(r.out, "SHAPED parts (routed, true-shape nesting HEURISTIC - not optimal): 1 part(s)"));
    EXPECT_TRUE(contains(r.out, "part spacing 8.2 mm, edge spacing 8.1 mm"));
    EXPECT_TRUE(contains(r.out, "parts: 1 required, 1 placed, 0 unplaced"));
    EXPECT_TRUE(contains(r.out, "validator (nesting::validateNest): no findings"));
    EXPECT_TRUE(contains(r.out, "cutting.adapter.routed"));
    EXPECT_TRUE(contains(r.out, "Wrote 2 sheet SVG(s) and 1 nesting sheet SVG(s) and cutting-summary.txt"));
    const std::string svg = readFile(u8path(out) / "mat.generic.pb18-nest-1.svg");
    EXPECT_TRUE(contains(svg, "<svg"));
    EXPECT_TRUE(contains(svg, "fos-part"));
    EXPECT_TRUE(std::filesystem::exists(u8path(out) / "mat.generic.pb18-sheet-1.svg"));
    EXPECT_TRUE(contains(readFile(u8path(out) / "cutting-summary.txt"), "nest sheet 1:"));
}

TEST_F(CliShaped, CamRoutesTheShapedShelf) {
    const std::string nc = dir.file("shelf.nc");
    const auto r = fos({"cam", project, "cab1/shelf.1", "--face", "A", "--out", nc});
    ASSERT_EQ(r.code, kExitOk) << r.err << r.out;
    EXPECT_TRUE(contains(r.out, "blank allowance 10 mm -> blank 782 x 560 x 18 mm"));
    EXPECT_TRUE(contains(r.out, "Contour routing: 1\n  contour.outline (outline, tool outside)"));
    EXPECT_TRUE(contains(r.out, "Simulation: 0 errors"));
    const std::string text = readFile(u8path(nc));
    EXPECT_TRUE(contains(text, "UNVERIFIED"));
    EXPECT_TRUE(contains(text, "\nG2 ") || contains(text, "\nG3 "));
    // Explicit allowance.
    const auto r5 = fos({"cam", project, "cab1/shelf.1", "--face", "A", "--out", nc, "--blank-allowance", "5"});
    EXPECT_EQ(r5.code, kExitOk) << r5.err;
    EXPECT_TRUE(contains(r5.out, "blank 772 x 550 x 18 mm"));
}

TEST_F(CliShaped, BlankAllowanceIsRefusedForRectangularParts) {
    const std::string nc = dir.file("side.nc");
    const auto r = fos({"cam", project, "cab1/side.left", "--face", "B", "--out", nc, "--blank-allowance", "10"});
    EXPECT_EQ(r.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(r.err, "--blank-allowance applies to shaped (routed) parts only"));
    EXPECT_FALSE(std::filesystem::exists(u8path(nc)));
    EXPECT_EQ(fos({"cam", project, "cab1/shelf.1", "--face", "A", "--out", nc, "--blank-allowance", "-1"}).code,
              kExitUsageOrIo);
    // Rectangular part without the option: unchanged behaviour.
    EXPECT_EQ(fos({"cam", project, "cab1/side.left", "--face", "B", "--out", nc}).code, kExitOk);
}

} // namespace
} // namespace fos::cli::test
