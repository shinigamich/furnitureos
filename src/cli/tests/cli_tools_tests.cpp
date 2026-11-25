// SPDX-License-Identifier: AGPL-3.0-only
#include "cli_test_support.hpp"

#include "fos/formats/tool_table.hpp"

namespace fos::cli::test {
namespace {

formats::ToolEntry endMill(const std::string& id, int number) {
    formats::ToolEntry t;
    t.id = id;
    t.number = number;
    t.name = "Own 6 mm cutter";
    t.type = cam::ToolKind::EndMill;
    t.diameter = 6;
    t.fluteLength = 22;
    t.maxDepthPerPass = 3;
    t.spindleRpm = 18000;
    t.feedRate = 3000;
    t.plungeRate = 1000;
    return t;
}

void save(const std::string& file, const formats::ToolTable& table) {
    formats::SaveOptions options;
    options.backupCount = 0;
    ASSERT_TRUE(formats::saveToolTable(u8path(file), table, formats::AppInfo{}, options).ok());
}

TEST(CliTools, ExportDefaultListAndValidate) {
    TempDir dir;
    const std::string file = dir.file("default.fostools");
    auto r = fos({"tools", "export-default", "-o", file});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    // demoToolLibrary(): 10 drills + 3 end mills.
    EXPECT_TRUE(contains(r.out, "Wrote " + file + " (13 tools of the FICTIONAL demo library"));
    EXPECT_EQ(fos({"tools", "export-default", "-o", file}).code, kExitUsageOrIo); // exists, no --force
    EXPECT_EQ(fos({"tools", "export-default", "--out", file, "--force"}).code, kExitOk);

    r = fos({"tools", "validate", file});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, ": 13 tool(s), 0 error(s), 0 warning(s)"));

    r = fos({"tools", "list", file});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Depth/pass")) << r.out;
    EXPECT_TRUE(contains(r.out, "T3   demo.drill.5    drill")) << r.out;
    EXPECT_TRUE(contains(r.out, "flat_end_mill")) << r.out;
    EXPECT_TRUE(contains(r.out, "Lengths in mm, feeds in mm/min."));
}

TEST(CliTools, ValidateReportsPathsAndExitCodes) {
    TempDir dir;
    formats::ToolTable table;
    table.tools = {endMill("a", 1), endMill("a", 2)};
    table.tools[1].maxDepthPerPass = 30; // > flute 22
    const std::string bad = dir.file("bad.fostools");
    // saveToolTable refuses invalid tables, so write the container directly.
    formats::Document doc;
    doc.format = "furnitureos-tools";
    doc.payload = formats::toolTableToJson(table);
    ASSERT_TRUE(formats::saveDocument(u8path(bad), doc, formats::SaveOptions{0, {}, {}, {}}).ok());
    auto r = fos({"tools", "validate", bad});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.out, "/payload/tools/1/id: duplicate id \"a\"")) << r.out;
    EXPECT_TRUE(contains(r.out, "/payload/tools/1/maxDepthPerPass: tool 'a' has max depth per pass 30 mm")) << r.out;
    EXPECT_TRUE(contains(r.out, "2 tool(s), 2 error(s)")) << r.out;
    EXPECT_EQ(fos({"tools", "list", bad}).code, kExitValidationErrors);

    { std::ofstream(u8path(dir.file("x.fostools"))) << "{\"format\": 1}"; }
    r = fos({"tools", "validate", dir.file("x.fostools")});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "fos tools validate: ")) << r.err;
    EXPECT_EQ(fos({"tools", "validate", dir.file("missing.fostools")}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"tools"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"tools", "frobnicate"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"tools", "list", bad, "--force"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"tools", "--help"}).code, kExitOk);
}

TEST(CliTools, CamUsesTheGivenToolTable) {
    TempDir dir;
    const std::string project = dir.file("k.fosp");
    ASSERT_EQ(newBaseCabinet(project).code, kExitOk);
    const std::string tools = dir.file("t.fostools");
    ASSERT_EQ(fos({"tools", "export-default", "-o", tools}).code, kExitOk);

    // The exported default table gives the same program as the built-in one.
    const std::string ncDefault = dir.file("a.nc"), ncFile = dir.file("b.nc");
    auto r = fos({"cam", project, "cab1/side.left", "--face", "A", "--out", ncDefault});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    r = fos({"cam", project, "cab1/side.left", "--face", "A", "--out", ncFile, "--tools", tools});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Tools: " + tools + " (user tool table")) << r.out;
    EXPECT_EQ(readFile(u8path(ncDefault)), readFile(u8path(ncFile)));

    // A table without drills: the side's holes cannot be drilled -> refused, nothing written.
    formats::ToolTable millsOnly;
    millsOnly.tools = {endMill("mill.6", 1)};
    const std::string mills = dir.file("mills.fostools");
    save(mills, millsOnly);
    const std::string nc = dir.file("c.nc");
    r = fos({"cam", project, "cab1/side.left", "--face", "A", "--out", nc, "--tools", mills});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "No drill of diameter")) << r.err;
    EXPECT_FALSE(std::filesystem::exists(u8path(nc)));

    // Feeds are optional in the file but CAM needs them.
    millsOnly.tools[0].feedRate.reset();
    save(mills, millsOnly);
    r = fos({"cam", project, "cab1/side.left", "--face", "A", "--out", nc, "--tools", mills});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "/payload/tools/0/feedRate: tool 'mill.6' has no feed rate")) << r.err;
    EXPECT_EQ(fos({"cam", project, "cab1/side.left", "--face", "A", "--out", nc, "--tools", dir.file("no.fostools")})
                  .code,
              kExitUsageOrIo);
}

} // namespace
} // namespace fos::cli::test
