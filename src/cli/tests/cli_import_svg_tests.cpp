// SPDX-License-Identifier: AGPL-3.0-only
// `fos import-svg` end to end.
#include "cli_test_support.hpp"

#include <fstream>

namespace fos::cli::test {
namespace {

// Worktop 1200 x 600 mm drawn in a 1200 x 600 viewBox (1 unit = 1 mm): sink 500 x 400 whose
// top-left corner is at (650, 100) in SVG (panel frame: x 650, y 600 - 100 - 400 = 100) and a tap
// hole R 20 centred at SVG (300, 400) -> panel (300, 200).
const std::string kWorktop =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200mm\" height=\"600mm\" viewBox=\"0 0 1200 600\">\n"
    "  <g transform=\"translate(0 0)\">\n"
    "    <path d=\"M0 0 H1200 V600 H0 Z\"/>\n"
    "    <rect x=\"650\" y=\"100\" width=\"500\" height=\"400\"/>\n"
    "    <circle cx=\"300\" cy=\"400\" r=\"20\"/>\n"
    "  </g>\n"
    "</svg>\n";

class CliImportSvg : public ::testing::Test {
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

TEST_F(CliImportSvg, ImportsWorktopAndShowsInInfo) {
    const auto r = fos({"import-svg", project, write("top.svg", kWorktop), "--name", "Work top", "--thickness", "18"});
    ASSERT_EQ(r.code, kExitOk) << r.err << r.out;
    EXPECT_TRUE(contains(r.out, "as item work-top (part work-top/panel), material mat.generic.mdf18")) << r.out;
    EXPECT_TRUE(contains(r.out, "Outline: 1200 x 600 mm, 4 segments (0 arcs); cut-outs: 2 (1 circle, 1 rectangle)")) << r.out;
    EXPECT_TRUE(contains(r.out, "SVG: 3 shape(s), units width/height 1200 x 600 mm over viewBox 1200 x 600")) << r.out;
    const auto info = fos({"info", project});
    ASSERT_EQ(info.code, kExitOk) << info.err;
    EXPECT_TRUE(contains(info.out, "work-top")) << info.out;
}

TEST_F(CliImportSvg, OutputFileScaleAndDefaultName) {
    // --scale 2: the 1200-unit outline becomes 2400 mm. -o leaves the input untouched.
    const std::string before = readFile(u8path(project));
    const std::string out = dir.file("out.fosp");
    const auto r = fos({"import-svg", project, write("Table top.svg", kWorktop), "--material", "mat.generic.pb18",
                        "--scale", "2", "-o", out});
    ASSERT_EQ(r.code, kExitOk) << r.err << r.out;
    EXPECT_TRUE(contains(r.out, "as item table-top")) << r.out;
    EXPECT_TRUE(contains(r.out, "Outline: 2400 x 1200 mm")) << r.out;
    EXPECT_EQ(readFile(u8path(project)), before);
    EXPECT_TRUE(contains(readFile(u8path(out)), "table-top"));
}

TEST_F(CliImportSvg, RefusalsAndBadInput) {
    const std::string before = readFile(u8path(project));
    const auto bez = fos({"import-svg", project, write("c.svg", "<svg><path d=\"M0 0 C 1 1 2 2 3 0 Z\"/></svg>"),
                          "--thickness", "18"});
    EXPECT_EQ(bez.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(bez.err, "Bezier curve command 'C'")) << bez.err;
    const auto ent = fos({"import-svg", project,
                          write("e.svg", "<!DOCTYPE s [<!ENTITY a \"aaaa\">]><svg>&a;</svg>"), "--thickness", "18"});
    EXPECT_EQ(ent.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(ent.err, "internal subset")) << ent.err;
    const auto tri = fos({"import-svg", project,
                          write("t.svg", "<svg width=\"800mm\" height=\"400mm\" viewBox=\"0 0 800 400\">"
                                         "<rect width=\"800\" height=\"400\"/><polygon points=\"100,100 300,100 200,250\"/></svg>"),
                          "--name", "Shelf", "--thickness", "18"});
    EXPECT_EQ(tri.code, kExitValidationErrors);
    EXPECT_TRUE(contains(tri.err, "project.import_svg.cutout_unsupported")) << tri.err;
    const auto open = fos({"import-svg", project, write("o.svg", "<svg><line x2=\"10\"/></svg>"), "--thickness", "18"});
    EXPECT_EQ(open.code, kExitValidationErrors);
    EXPECT_EQ(readFile(u8path(project)), before);
    EXPECT_EQ(fos({"import-svg", project, dir.file("t.svg")}).code, kExitUsageOrIo); // no material
    EXPECT_EQ(fos({"import-svg", project, dir.file("t.svg"), "--thickness", "18", "--scale", "0"}).code, kExitUsageOrIo);
    EXPECT_TRUE(contains(fos({"import-svg", "--help"}).out, "Bezier"));
}

} // namespace
} // namespace fos::cli::test
