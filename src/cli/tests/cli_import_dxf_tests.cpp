// SPDX-License-Identifier: AGPL-3.0-only
// `fos import-dxf` end to end: import a worktop outline, then `fos info` and `fos drawing`.
#include "cli_test_support.hpp"

#include <fstream>

namespace fos::cli::test {
namespace {

std::string dxfFile(const std::string& entities) {
    return "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" + entities +
           "0\nENDSEC\n0\nEOF\n";
}

std::string closedPoly(const std::vector<std::pair<int, int>>& pts) {
    std::string s = "0\nLWPOLYLINE\n8\nCUT\n90\n" + std::to_string(pts.size()) + "\n70\n1\n";
    for (const auto& [x, y] : pts) s += "10\n" + std::to_string(x) + "\n20\n" + std::to_string(y) + "\n";
    return s;
}

// Worktop 1200 x 600 drawn at (500, 500): sink 500 x 400 at part (650, 100), tap hole R 20 at
// part (300, 200).
const std::string kWorktop =
    dxfFile(closedPoly({{500, 500}, {1700, 500}, {1700, 1100}, {500, 1100}}) +
            closedPoly({{1150, 600}, {1650, 600}, {1650, 1000}, {1150, 1000}}) +
            "0\nCIRCLE\n8\nCUT\n10\n800\n20\n700\n40\n20\n");

class CliImportDxf : public ::testing::Test {
protected:
    TempDir dir;
    std::string project = dir.file("kitchen.fosp");
    std::string write(const std::string& name, const std::string& text) {
        const std::string path = dir.file(name);
        std::ofstream(u8path(path), std::ios::binary) << text;
        return path;
    }
    void SetUp() override { ASSERT_EQ(newBaseCabinet(project).code, kExitOk); }
};

TEST_F(CliImportDxf, ImportedPanelShowsInInfoAndDrawing) {
    const auto r = fos({"import-dxf", project, write("top.dxf", kWorktop), "--name", "Work top", "--thickness", "18"});
    ASSERT_EQ(r.code, kExitOk) << r.err << r.out;
    EXPECT_TRUE(contains(r.out, "as item work-top (part work-top/panel), material mat.generic.mdf18")) << r.out;
    EXPECT_TRUE(contains(r.out, "Outline: 1200 x 600 mm, 4 segments (0 arcs); cut-outs: 2 (1 circle, 1 rectangle)"));
    const auto info = fos({"info", project});
    ASSERT_EQ(info.code, kExitOk) << info.err;
    EXPECT_TRUE(contains(info.out, "work-top")) << info.out;
    EXPECT_TRUE(contains(info.out, "Work top"));
    const std::string svg = dir.file("top.svg");
    const auto d = fos({"drawing", project, "work-top/panel", "--svg", svg});
    ASSERT_EQ(d.code, kExitOk) << d.err << d.out;
    EXPECT_TRUE(contains(readFile(u8path(svg)), "<svg"));
    // A second import gets a unique id.
    const auto again = fos({"import-dxf", project, dir.file("top.dxf"), "--name", "Work top", "--material",
                            "mat.generic.pb18"});
    ASSERT_EQ(again.code, kExitOk) << again.err;
    EXPECT_TRUE(contains(again.out, "as item work-top-2"));
}

TEST_F(CliImportDxf, UnsupportedCutoutRefusesUnlessSkipped) {
    const std::string tri = write("tri.dxf", dxfFile(closedPoly({{0, 0}, {800, 0}, {800, 400}, {0, 400}}) +
                                                     closedPoly({{100, 100}, {300, 100}, {200, 250}})));
    const std::string before = readFile(u8path(project));
    const auto r = fos({"import-dxf", project, tri, "--name", "Shelf", "--material", "mat.generic.pb18"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "neither a circle nor an axis-parallel (rounded) rectangle")) << r.err;
    EXPECT_EQ(readFile(u8path(project)), before);
    const auto s = fos({"import-dxf", project, tri, "--name", "Shelf", "--material", "mat.generic.pb18",
                        "--skip-unsupported-cutouts"});
    EXPECT_EQ(s.code, kExitOk) << s.err;
    EXPECT_TRUE(contains(s.out, "1 left out"));
}

TEST_F(CliImportDxf, BadInputExitCodes) {
    const auto m = fos({"import-dxf", project, write("bad.dxf", "0\nSECTION\n2\nENTITIES\n0\nLINE\n10\nabc\n0\nENDSEC\n0\nEOF\n"),
                        "--name", "X", "--material", "mat.generic.pb18"});
    EXPECT_EQ(m.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(m.err, "DXF line 8: group code 10 needs a finite number")) << m.err;
    const auto open = fos({"import-dxf", project, write("open.dxf", dxfFile("0\nLINE\n10\n0\n20\n0\n11\n5\n21\n0\n")),
                           "--name", "X", "--material", "mat.generic.pb18"});
    EXPECT_EQ(open.code, kExitValidationErrors);
    EXPECT_TRUE(contains(open.err, "formats.dxf.open_chain")) << open.err;
    EXPECT_EQ(fos({"import-dxf", project, dir.file("top.dxf"), "--name", "X"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"import-dxf", project, dir.file("missing.dxf"), "--name", "X", "--thickness", "18"}).code,
              kExitUsageOrIo);
    EXPECT_EQ(fos({"import-dxf", project, dir.file("x.dxf"), "--name", "X", "--material", "nope"}).code,
              kExitUsageOrIo);
}

TEST_F(CliImportDxf, ExportDxfRoundTripsThroughImport) {
    ASSERT_EQ(fos({"import-dxf", project, write("w.dxf", kWorktop), "--name", "Work top", "--thickness", "18"}).code,
              kExitOk);
    const std::string out = dir.file("top.dxf");
    const auto e = fos({"export-dxf", project, out, "--part", "work-top/panel"});
    ASSERT_EQ(e.code, kExitOk) << e.err;
    EXPECT_TRUE(contains(e.out, "Wrote 1 part(s)")) << e.out;
    const std::string text = readFile(out);
    EXPECT_TRUE(contains(text, "AC1009"));
    EXPECT_TRUE(contains(text, "\nCUTOUTS\n"));
    // Re-importing the exported file reproduces the panel (same summary line as the first import).
    const auto again = fos({"import-dxf", project, out, "--name", "Copy", "--thickness", "18"});
    ASSERT_EQ(again.code, kExitOk) << again.err;
    EXPECT_TRUE(contains(again.out, "Outline: 1200 x 600 mm, 4 segments (0 arcs); cut-outs: 2 (1 circle, 1 rectangle)"))
        << again.out;

    const std::string each = dir.file("parts");
    const auto f = fos({"export-dxf", project, each, "--one-file-per-part", "--finished"});
    ASSERT_EQ(f.code, kExitOk) << f.err;
    EXPECT_TRUE(std::filesystem::exists(u8path(each + "/work-top_panel.dxf")));
    EXPECT_TRUE(std::filesystem::exists(u8path(each + "/copy_panel.dxf")));
    EXPECT_EQ(fos({"export-dxf", project, out, "--part", "no/such"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"export-dxf", project}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"export-dxf", project, out, "--spacing", "-1"}).code, kExitUsageOrIo);
}

} // namespace
} // namespace fos::cli::test
