// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/tool_table.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

namespace fos::formats::test {
namespace {

ToolEntry drill5() {
    ToolEntry t;
    t.id = "drill.5";
    t.number = 1;
    t.name = "Brad point 5";
    t.type = cam::ToolKind::Drill;
    t.diameter = 5;
    t.fluteLength = 30;
    t.overallLength = 70;
    t.spindleRpm = 6000;
    t.feedRate = 1500;
    t.plungeRate = 1500;
    t.notes = "user data";
    return t;
}

ToolEntry mill6() {
    ToolEntry t;
    t.id = "mill.6";
    t.number = 2;
    t.type = cam::ToolKind::EndMill;
    t.diameter = 6;
    t.fluteLength = 22;
    t.maxDepthPerPass = 3;
    t.centerCutting = false;
    t.spindleRpm = 18000;
    t.feedRate = 3000;
    t.plungeRate = 1000;
    return t;
}

bool hasFinding(const core::Diagnostics& ds, const std::string& path, const std::string& text) {
    for (const auto& d : ds)
        if (d.entityId == path && d.message.find(text) != std::string::npos) return true;
    return false;
}

TEST(ToolTable, FileRoundTripKeepsEveryFieldAndExtraKeys) {
    TempDir dir;
    ToolTable table;
    table.tools = {drill5(), mill6()};
    table.extra["shop"] = "north";
    const auto file = dir / "tools.fostools";
    auto saved = saveToolTable(file, table, AppInfo{"FurnitureOS", "test"}, SaveOptions{0, {}, {}, {}});
    ASSERT_TRUE(saved.ok()) << saved.error().message;

    auto loaded = loadToolTable(file);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_TRUE(loaded.value().diagnostics.empty());
    const auto& t = loaded.value().table;
    ASSERT_EQ(t.tools.size(), 2u);
    EXPECT_EQ(t.tools[0].id, "drill.5");
    EXPECT_EQ(t.tools[0].name, "Brad point 5");
    EXPECT_EQ(t.tools[0].overallLength, 70.0);
    EXPECT_FALSE(t.tools[0].maxDepthPerPass.has_value());
    EXPECT_EQ(t.tools[0].notes, "user data");
    EXPECT_EQ(t.tools[1].type, cam::ToolKind::EndMill);
    EXPECT_EQ(t.tools[1].maxDepthPerPass, 3.0);
    EXPECT_FALSE(t.tools[1].centerCutting);
    EXPECT_FALSE(t.tools[1].overallLength.has_value());
    EXPECT_EQ(t.extra["shop"], "north");
    // Same payload again -> identical JSON.
    EXPECT_EQ(toolTableToJson(t), toolTableToJson(table));
    const std::string text = readText(file);
    EXPECT_NE(text.find("\"format\": \"furnitureos-tools\""), std::string::npos);
    EXPECT_NE(text.find("\"type\": \"flat_end_mill\""), std::string::npos);
}

TEST(ToolTable, ConvertsToCamLibraryAndBack) {
    ToolTable table;
    table.tools = {mill6(), drill5()};
    auto lib = toToolLibrary(table);
    ASSERT_TRUE(lib.ok()) << lib.error().message;
    ASSERT_EQ(lib.value().tools().size(), 2u);
    const cam::Tool* m = lib.value().findById("mill.6");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->maxDepthOfCut, 3.0);
    EXPECT_EQ(m->rpm, 18000.0);
    // Library order is by tool number: drill.5 (T1) first.
    const ToolTable back = toolTableFromLibrary(lib.value());
    EXPECT_EQ(back.tools[0].id, "drill.5");
    EXPECT_EQ(back.tools[1].maxDepthPerPass, 3.0);
    EXPECT_TRUE(validateToolTable(back).empty());
}

TEST(ToolTable, CamConversionNeedsSpeedsAndFeeds) {
    ToolTable table;
    table.tools = {drill5()};
    table.tools[0].spindleRpm.reset();
    EXPECT_TRUE(validateToolTable(table).empty()); // optional in the file
    auto lib = toToolLibrary(table);
    ASSERT_FALSE(lib.ok());
    EXPECT_EQ(lib.error().message,
              "/payload/tools/0/spindleRpm: tool 'drill.5' has no spindle speed; CAM needs one for every tool.");
}

TEST(ToolTable, ValidationListsEveryProblemWithItsPath) {
    ToolTable table;
    ToolEntry a = mill6();
    a.maxDepthPerPass = 25; // > flute length 22
    a.overallLength = 20;   // < flute length 22
    ToolEntry b = drill5();
    b.id = "mill.6";        // duplicate id
    b.number = 2;           // duplicate number
    b.diameter = 0;
    b.maxDepthPerPass = 2;  // unused on drills -> warning
    ToolEntry c = mill6();
    c.id = "mill.8";
    c.number = 0;
    c.maxDepthPerPass.reset();
    c.feedRate = -5;
    table.tools = {a, b, c};
    const auto ds = validateToolTable(table);
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/0/maxDepthPerPass",
                           "tool 'mill.6' has max depth per pass 25 mm, more than its flute length 22 mm."));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/0/overallLength", "overall length 20 mm, shorter than"));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/1/id", "duplicate id \"mill.6\" (already used by /payload/tools/0)"));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/1/number", "T2, already taken by 'mill.6'"));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/1/diameter", "diameter 0 mm; it must be greater than 0."));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/1/maxDepthPerPass", "not used for drills"));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/2/number", "tool number 0; tool numbers must be >= 1."));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/2/maxDepthPerPass", "flat end mill without max depth per pass"));
    EXPECT_TRUE(hasFinding(ds, "/payload/tools/2/feedRate", "feed rate -5 mm/min"));
    EXPECT_EQ(ds.size(), 9u);
    std::size_t warnings = 0;
    for (const auto& d : ds) warnings += d.severity == core::Severity::Warning ? 1u : 0u;
    EXPECT_EQ(warnings, 1u);
    ASSERT_FALSE(toToolLibrary(table).ok());

    ToolTable empty;
    EXPECT_TRUE(hasFinding(validateToolTable(empty), "/payload/tools", "has no tools"));
}

TEST(ToolTable, SaveRefusesInvalidTable) {
    TempDir dir;
    ToolTable table;
    table.tools = {mill6()};
    table.tools[0].fluteLength = 2; // max depth per pass 3 > 2
    auto saved = saveToolTable(dir / "x.fostools", table, AppInfo{});
    ASSERT_FALSE(saved.ok());
    EXPECT_NE(saved.error().message.find("/payload/tools/0/maxDepthPerPass"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(dir / "x.fostools"));
}

TEST(ToolTable, StructuralErrorsNameThePath) {
    auto decode = [](const char* json) { return toolTableFromJson(Json::parse(json), "/payload"); };
    auto r = decode(R"({"tools":[{"id":"a","number":1,"type":"ball_end_mill","diameter":6,"fluteLength":20}]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools/0/type");
    EXPECT_NE(r.error().message.find("not modelled"), std::string::npos);
    r = decode(R"({"tools":[{"id":"a","number":1.5,"type":"drill","diameter":6,"fluteLength":20}]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools/0/number");
    r = decode(R"({"tools":[{"id":"a","number":1e300,"type":"drill","diameter":6,"fluteLength":20}]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools/0/number");
    r = decode(R"({"tools":[{"id":"a","number":1,"type":"drill","diameter":"6","fluteLength":20}]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools/0/diameter");
    r = decode(R"({"tools":[{"id":"a","number":1,"type":"drill","diameter":6,"fluteLenght":20}]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools/0/fluteLength"); // required key missing comes first
    r = decode(R"({"tools":[{"id":"a","number":1,"type":"drill","diameter":6,"fluteLength":20,"colour":1}]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools/0/colour");
    r = decode(R"({"tool":[]})");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context, "/payload/tools");
}

TEST(ToolTable, RejectsOtherKindsAndTamperedFiles) {
    TempDir dir;
    ToolTable table;
    table.tools = {drill5()};
    const auto file = dir / "t.fostools";
    ASSERT_TRUE(saveToolTable(file, table, AppInfo{}, SaveOptions{0, {}, {}, {}}).ok());
    std::string text = readText(file);
    const auto pos = text.find("\"diameter\": 5");
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, 13, "\"diameter\": 6");
    auto r = parseToolTableText(text, "t.fostools");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::CorruptData);
    LoadOptions recover;
    recover.allowChecksumMismatch = true;
    r = parseToolTableText(text, "t.fostools", recover);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().table.tools[0].diameter, 6.0);
    EXPECT_FALSE(r.value().diagnostics.empty()); // checksum warning

    r = parseToolTableText(R"({"format":"furnitureos-library","formatVersion":1,"payload":{}})", "x");
    ASSERT_FALSE(r.ok());
}

} // namespace
} // namespace fos::formats::test
